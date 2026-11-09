#!/usr/bin/env python3
"""Experiment3 non-halting OpenOCD Tcl fixture. Never flashes or drives actuators."""
import argparse
import json
import secrets
import socket
import struct
import subprocess
import time
from pathlib import Path


class Probe:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3)

    def call(self, command):
        self.sock.sendall(command.encode("ascii") + b"\x1a")
        reply = bytearray()
        while not reply.endswith(b"\x1a"):
            chunk = self.sock.recv(65536)
            if not chunk:
                raise RuntimeError("OpenOCD disconnected")
            reply.extend(chunk)
        return reply[:-1].decode("ascii").strip()

    def read(self, address, count):
        reply = self.call(
            f"mem2array e3 32 {address} {count}; set e3out {{}}; "
            f"for {{set i 0}} {{$i < {count}}} {{incr i}} {{lappend e3out $e3($i)}}; "
            "unset e3; set e3out")
        values = [int(v, 0) for v in reply.split()]
        if len(values) != count:
            raise RuntimeError(f"Unexpected memory response: {reply}")
        return values

    def write(self, address, value):
        reply = self.call(f"mww {address} {value}")
        if reply:
            raise RuntimeError(reply)


def symbols(elf):
    result = {}
    for line in subprocess.check_output(["arm-none-eabi-nm", "-S", str(elf)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 4:
            result[fields[3]] = (int(fields[0], 16), int(fields[1], 16))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("operation", choices=["new", "arm", "send", "capture"])
    p.add_argument("--session", type=Path, required=True)
    p.add_argument("--elf", type=Path)
    p.add_argument("--port", type=int, default=6666)
    p.add_argument("--kind", choices=["confirm", "cancel", "query"], default="confirm")
    p.add_argument("--action", type=int, default=1)
    p.add_argument("--request", type=int, default=1)
    p.add_argument("--context", type=int, default=1)
    p.add_argument("--target", type=int, default=1)
    p.add_argument("--generation", type=int, default=1)
    p.add_argument("--seconds", type=float, default=90)
    p.add_argument("--output", type=Path)
    a = p.parse_args()
    if a.operation == "new":
        # Never overwrite a run: after either MCU reboot, reset BOTH and create a NEW file.
        with a.session.open("x") as f:
            json.dump({"controller": secrets.randbelow(0xffffffff)+1,
                       "hmi": secrets.randbelow(0xffffffff)+1,
                       "created_ns": time.time_ns()}, f, indent=2)
        return
    if not a.elf:
        p.error("--elf is required")
    session = json.loads(a.session.read_text())
    sym = symbols(a.elf)
    base, size = sym["g_exp3_hil"]
    box, box_size = sym["g_exp3_mailbox"]
    capacity = (size - 252) // 16
    if box_size != 36 or size != 252 + capacity * 16 or capacity < 1:
        raise RuntimeError("Incompatible Experiment3 debug ABI")
    probe = Probe(a.port)
    if a.operation == "capture":
        if not a.output:
            p.error("capture requires --output")
        end = time.monotonic() + a.seconds
        with a.output.open("x") as f:
            while time.monotonic() < end:
                write, read, overflow = probe.read(base + 236, 3)
                count = (write - read) & 0xffffffff
                if count > capacity:
                    raise RuntimeError("Ring reset/corruption; stop and provision a fresh paired run")
                if count:
                    words = probe.read(base + 248, capacity * 4)
                    for seq in range(count):
                        index = ((read + seq) % capacity) * 4
                        tick, x, y, event = words[index:index+4]
                        f.write(json.dumps(dict(host_ns=time.monotonic_ns(), tick_ms=tick,
                                                event=event, a=x, b=y, overflow=overflow)) + "\n")
                    f.flush()
                    probe.write(base + 232, write)
                capture_overflow = probe.read(sym["g_exp3_capture_overflow"][0], 1)[0]
                if overflow or capture_overflow:
                    f.write(json.dumps(dict(log_overflow=overflow, capture_overflow=capture_overflow)) + "\n")
                    f.flush()
                time.sleep(.005)
        return
    if probe.read(box + 32, 1)[0]:
        raise RuntimeError("Mailbox busy; do not overwrite pending command")
    bound = (probe.read(base + size - 4, 1)[0] >> 16) & 255
    if a.operation == "arm" and bound:
        raise RuntimeError("Already bound. Reboot BOTH endpoints and use a NEW session file")
    if a.operation == "send":
        if not bound or probe.read(base + 212, 2) != [session["controller"], session["hmi"]]:
            raise RuntimeError("Endpoint is not bound to this session")
        if "endpoint" in sym:  # STM32 test endpoint must never originate a command.
            raise RuntimeError("send requires the OpenI6X ELF/probe")
    kind = {"confirm": 1, "cancel": 2, "query": 3}[a.kind]
    raw = struct.pack("<6IH4B2x", session["controller"], session["hmi"], a.request,
                      a.action, a.context, a.generation, a.target, kind, 1, 0, 0)
    for i, word in enumerate(struct.unpack("<8I", raw)):
        probe.write(box + 4*i, word)
    probe.write(box + 32, 0x45334152 if a.operation == "arm" else 0x45335344)
    print("Posted; verify BIND/TX in capture (posting is not receipt or success).")


if __name__ == "__main__":
    main()

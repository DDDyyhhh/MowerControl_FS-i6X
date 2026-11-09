#!/usr/bin/env python3
"""Adapt only two Linux host-fixture details to Apple's C compiler/linker."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

arguments = sys.argv[1:]
if any("remote-rx-queue-" in arg and arg.endswith("/queue.c") for arg in arguments):
    arguments.append("-Wno-unused-function")

wrapped = [arg.split("=", 1)[1] for arg in arguments if arg.startswith("-Wl,--wrap=")]
if not wrapped:
    os.execv("/usr/bin/cc", ["cc", *arguments])

expected = {"RemoteAuthorization_Advance", "FlySkyIbusServo_FeedReceived", "FlySkyIbusServo_Advance"}
if set(wrapped) != expected:
    raise SystemExit("Unsupported host --wrap request; refusing to omit an interposition")
sources = [arg for arg in arguments if arg.endswith(".c")]
if {Path(arg).name for arg in sources} != {
    "ibus_snapshot_harness.c", "remote_backend.c", "remote_authorization.c",
    "flysky_ibus_servo.c", "blade_confirm_ch6_adapter.c", "grass_bag_control.c",
}:
    raise SystemExit("Unexpected snapshot fixture source list")

output_index = arguments.index("-o")
output = arguments[output_index + 1]
flags = [arg for index, arg in enumerate(arguments)
         if index not in (output_index, output_index + 1)
         and arg not in sources and not arg.startswith("-Wl,--wrap=") and arg != "-lm"]

# GNU --wrap changes undefined references only. The three cross-TU calls are
# in remote_backend.c; leave definitions and same-TU calls unmodified. The
# harness's __real_ references bind to the original production definitions.
with tempfile.TemporaryDirectory(prefix="mower-macos-wrap-") as temporary:
    objects = []
    for index, source in enumerate(sources):
        aliases = []
        if Path(source).name == "remote_backend.c":
            aliases = [f"-D{name}=__wrap_{name}" for name in wrapped]
        elif Path(source).name == "ibus_snapshot_harness.c":
            aliases = [f"-D__real_{name}={name}" for name in wrapped]
        obj = str(Path(temporary) / f"{index}.o")
        subprocess.run(["/usr/bin/cc", *flags, *aliases, "-c", source, "-o", obj], check=True)
        objects.append(obj)
    sanitizers = [arg for arg in flags if arg.startswith("-fsanitize=")]
    subprocess.run(["/usr/bin/cc", *objects, *sanitizers, "-lm", "-o", output], check=True)

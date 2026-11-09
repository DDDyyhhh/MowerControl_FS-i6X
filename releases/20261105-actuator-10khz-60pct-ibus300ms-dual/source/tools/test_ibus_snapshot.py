#!/usr/bin/env python3
"""Exercise the real backend with deterministic task/IRQ scheduling."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "control_chassis/Core"
SOURCES = [
    ROOT / "control_chassis/host/ibus_snapshot_harness.c",
    *[CORE / "UserDev" / name for name in (
        "remote_backend.c", "remote_authorization.c", "flysky_ibus_servo.c",
        "blade_confirm_ch6_adapter.c", "grass_bag_control.c",
    )],
]

with tempfile.TemporaryDirectory(prefix="ibus-snapshot-") as directory:
    for timeout_ms in (100, 300):
        for optimization in ("-O0", "-O2"):
            for production in (0, 1):
                executable = Path(directory) / "harness"
                subprocess.run([
                    os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror",
                    optimization, "-DREMOTE_BACKEND_HOST",
                    f"-DREMOTE_IBUS_TIMEOUT_MS={timeout_ms}U",
                    "-DREMOTE_BACKEND_IRQ_TEST", "-DSENS_IBUS_HIL_DIAG_USART1_TX_ENABLED=1",
                    "-include", str(ROOT / "control_chassis/host/fakes/ibus_snapshot_irq.h"),
                    "-DREMOTE_BACKEND_A28_E34=0", "-DREMOTE_BACKEND_FLYSKY_IBUS=1",
                    f"-DMOWER_BLADE_OK_ENABLED={production}",
                    f"-DMOWER_GRASS_BAG_CH6_ENABLED={production}",
                    "-I" + str(CORE / "Inc"), *map(str, SOURCES), "-lm",
                    "-Wl,--wrap=RemoteAuthorization_Advance",
                    "-Wl,--wrap=FlySkyIbusServo_FeedReceived",
                    "-Wl,--wrap=FlySkyIbusServo_Advance", "-o", str(executable),
                ], check=True)
                subprocess.run([str(executable)], check=True, timeout=5)
                print(f"PASS {optimization} production={production} timeout_ms={timeout_ms}", flush=True)

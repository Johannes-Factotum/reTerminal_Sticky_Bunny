#!/usr/bin/env python3
"""Flash the Sticky and check its boot log over USB serial.

Run on the computer the Sticky is plugged into:

  python3 tools/device_smoke_test.py --port /dev/ttyUSB0 --erase   # first install
  python3 tools/device_smoke_test.py --port /dev/ttyUSB0           # later updates
  python3 tools/device_smoke_test.py --port /dev/ttyUSB0 --no-flash --seconds 120

After flashing it resets the board and watches the log. While it listens,
open the launcher and try each app (see docs/device_test_checklist.md);
every app start, Wi-Fi start and save is logged and checked here.
Exit code 0 = all required markers seen and no errors.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time

REQUIRED = {
    "boot finished": re.compile(r"phase=ready result=ok"),
    "launcher with 6 apps": re.compile(r"launcher=ready .*apps=6"),
}
OPTIONAL = {
    "Cards opened": re.compile(r"app_to=flashcards"),
    "Cards progress saved": re.compile(r"flashcards=storage save .*result=ESP_OK"),
    "To-Do opened": re.compile(r"app_to=todo"),
    "Habits opened": re.compile(r"app_to=habits"),
    "Habits routine completed": re.compile(r"habits=routine complete"),
    "Habits Wi-Fi hotspot": re.compile(r"habits=web started"),
    "Habits saved from phone": re.compile(r"habits=config updated"),
    "Draftling page": re.compile(r"draftling=probe installed=\d"),
    "Pomodoro jingle": re.compile(r"buzzer=alarm state=started pattern=jingle"),
}
FAILURES = re.compile(
    r"Guru Meditation|abort\(\)|panic|Backtrace:|result=failed|"
    r"stack overflow|E \(\d+\)")


def run(cmd: list[str]) -> None:
    print("$", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--env", default="sticky-release")
    parser.add_argument("--erase", action="store_true",
                        help="erase flash first (needed once: new partition table)")
    parser.add_argument("--no-flash", action="store_true")
    parser.add_argument("--seconds", type=int, default=90)
    args = parser.parse_args()

    if not args.no_flash:
        if args.erase:
            run(["pio", "run", "-e", args.env, "-t", "erase",
                 "--upload-port", args.port])
        run(["pio", "run", "-e", args.env, "-t", "upload",
             "--upload-port", args.port])

    import serial  # pyserial ships with PlatformIO

    seen: set[str] = set()
    failures: list[str] = []
    with serial.Serial(args.port, 115200, timeout=0.5) as port:
        # Pulse EN through RTS to restart and catch the boot log.
        port.dtr = False
        port.rts = True
        time.sleep(0.2)
        port.rts = False
        deadline = time.time() + args.seconds
        print(f"--- listening {args.seconds} s; use the apps now ---", flush=True)
        while time.time() < deadline:
            line = port.readline().decode("utf-8", "replace").rstrip()
            if not line:
                continue
            print(line, flush=True)
            for name, pattern in {**REQUIRED, **OPTIONAL}.items():
                if name not in seen and pattern.search(line):
                    seen.add(name)
                    print(f"    [seen] {name}", flush=True)
            if FAILURES.search(line):
                failures.append(line)

    print("\n=== Summary ===")
    ok = True
    for name in REQUIRED:
        mark = "PASS" if name in seen else "FAIL"
        ok &= name in seen
        print(f"{mark:5} {name}")
    for name in OPTIONAL:
        print(f"{'seen' if name in seen else '-':5} {name}")
    if failures:
        ok = False
        print(f"\n{len(failures)} error line(s):")
        for line in failures[:20]:
            print("  ", line)
    print("\nRESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

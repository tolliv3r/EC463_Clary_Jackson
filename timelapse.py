#!/usr/bin/env python3
"""Take a photo with the Insta360 camera every N minutes.

Photos are saved into ~/images (created if missing). The script shells out
to photo.sh (which wraps camera_control) so it reuses the existing SDK
LD_LIBRARY_PATH setup.

Usage:
    ./timelapse.py                 # default 5-minute interval
    ./timelapse.py --interval 10   # every 10 minutes
    ./timelapse.py --output ~/pics # custom output directory
"""

from __future__ import annotations

import argparse
import logging
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PHOTO_SH = SCRIPT_DIR / "helper" / "photo.sh"

_running = True


def _handle_signal(signum, _frame):
    global _running
    logging.info("received signal %s, stopping after current capture", signum)
    _running = False


def take_photo(output_dir: Path, timeout: int) -> bool:
    """Invoke photo.sh to capture a photo into output_dir. Returns True on success."""
    if not PHOTO_SH.exists():
        logging.error("photo.sh not found at %s", PHOTO_SH)
        return False

    cmd = [str(PHOTO_SH), str(output_dir)]
    logging.info("capturing photo -> %s", output_dir)
    try:
        result = subprocess.run(
            cmd,
            timeout=timeout,
            capture_output=True,
            text=True,
            check=False,
        )
    except subprocess.TimeoutExpired:
        logging.error("photo capture timed out after %ss", timeout)
        return False
    except Exception as exc:
        logging.error("failed to run photo.sh: %s", exc)
        return False

    if result.returncode != 0:
        logging.error(
            "photo.sh exited %s. stdout=%r stderr=%r",
            result.returncode,
            result.stdout.strip(),
            result.stderr.strip(),
        )
        return False

    if result.stdout.strip():
        logging.info("photo.sh stdout: %s", result.stdout.strip())
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--interval",
        type=float,
        default=5.0,
        help="Minutes between photos (default: 5)",
    )
    parser.add_argument(
        "--output",
        type=str,
        default="~/images",
        help="Directory to save photos (default: ~/images)",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=120,
        help="Per-capture timeout in seconds (default: 120)",
    )
    parser.add_argument(
        "--startup-delay",
        type=float,
        default=0.0,
        help="Seconds to wait before the first photo (default: 0). Useful "
             "when starting at boot to let the camera finish booting.",
    )
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
        stream=sys.stdout,
    )

    output_dir = Path(os.path.expanduser(args.output)).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    logging.info("output directory: %s", output_dir)

    interval_sec = max(1.0, args.interval * 60.0)
    logging.info("interval: %.1f minutes (%.0fs)", args.interval, interval_sec)

    signal.signal(signal.SIGINT, _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)

    if args.startup_delay > 0:
        logging.info(
            "startup delay: waiting %.0fs before first photo",
            args.startup_delay,
        )
        remaining = args.startup_delay
        while _running and remaining > 0:
            chunk = min(remaining, 5.0)
            time.sleep(chunk)
            remaining -= chunk
        if not _running:
            logging.info("interrupted during startup delay")
            return 0

    next_shot = time.monotonic()
    while _running:
        start = time.monotonic()
        ok = take_photo(output_dir, args.timeout)
        if not ok:
            logging.warning("capture failed; will retry at next interval")

        # drift-free schedule: advance next_shot until it is in the future
        next_shot += interval_sec
        now = time.monotonic()
        if next_shot <= now:
            missed = int((now - next_shot) // interval_sec) + 1
            logging.warning("behind schedule, skipping %d slot(s)", missed)
            next_shot += missed * interval_sec

        sleep_for = next_shot - now
        logging.info("sleeping %.1fs until next capture", sleep_for)
        # wake periodically so signals can stop us promptly
        while _running and sleep_for > 0:
            chunk = min(sleep_for, 5.0)
            time.sleep(chunk)
            sleep_for = next_shot - time.monotonic()

        _ = start  # reserved for future metrics

    logging.info("exiting cleanly")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""
Live camera/target tracking debugger for Motive editor.

Polls /profile/scene and prints concise warnings/metrics while you control
the character/camera.

Usage:
    python3 watch_camera_tracking.py [port] [hz]
Examples:
    python3 watch_camera_tracking.py 40130
    python3 watch_camera_tracking.py 40130 10
"""

import json
import sys
import time
import urllib.error
import urllib.request


def fetch_json(url, timeout=2.0):
    req = urllib.request.Request(url, method="GET")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def fmt3(values):
    if not isinstance(values, list) or len(values) < 3:
        return "n/a"
    return f"({values[0]: .3f}, {values[1]: .3f}, {values[2]: .3f})"


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 40130
    hz = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0
    interval = 1.0 / max(1.0, hz)
    base = f"http://127.0.0.1:{port}"

    print(f"watching {base}/profile/scene at {hz:.1f} Hz (Ctrl+C to stop)")
    print("-" * 110)

    while True:
        t0 = time.time()
        try:
            profile = fetch_json(f"{base}/profile/scene")
            tracking = profile.get("cameraTracking", {})
            if not tracking.get("ok"):
                print(f"cameraTracking unavailable: {tracking.get('error', 'unknown error')}")
            else:
                warnings = tracking.get("warnings", [])
                warn = ",".join(warnings) if warnings else "-"
                print(
                    f"cam='{tracking.get('cameraName','')}' id={tracking.get('cameraId','')[:8]} "
                    f"freeFly={tracking.get('freeFly')} follow={tracking.get('followMode')} "
                    f"followScene={tracking.get('followSceneIndex')} "
                    f"targetScene={tracking.get('targetSceneIndex')} "
                    f"dist={tracking.get('distanceToTarget', 0.0):.3f} "
                    f"dot={tracking.get('frontDotToTarget', 0.0):.3f} "
                    f"ndc={fmt3(tracking.get('targetNdc'))} "
                    f"behind={tracking.get('targetBehindCamera')} offscreen={tracking.get('targetOffscreen')} "
                    f"warn=[{warn}]"
                )
        except urllib.error.HTTPError as exc:
            print(f"HTTP error: {exc.code} {exc.reason}")
        except Exception as exc:
            print(f"request error: {exc}")

        elapsed = time.time() - t0
        sleep_for = interval - elapsed
        if sleep_for > 0:
            time.sleep(sleep_for)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass

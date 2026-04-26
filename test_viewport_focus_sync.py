#!/usr/bin/env python3
"""
Viewport focus sync REST test.

Verifies that selecting a camera (same path hierarchy uses) keeps viewport
focus/highlight state aligned with the selected camera.

Usage:
    python3 test_viewport_focus_sync.py [port]
"""

import json
import sys
import time
import urllib.error
import urllib.request


class MotiveApiClient:
    def __init__(self, port=40132):
        self.base_url = f"http://127.0.0.1:{port}"

    def _call(self, method, endpoint, data=None, timeout=5):
        url = f"{self.base_url}{endpoint}"
        headers = {"Content-Type": "application/json"}
        try:
            if data is None:
                req = urllib.request.Request(url, method=method)
            else:
                body = json.dumps(data).encode("utf-8")
                req = urllib.request.Request(url, data=body, headers=headers, method=method)
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            return {"ok": False, "error": f"HTTP {exc.code}: {exc.reason}"}
        except Exception as exc:
            return {"ok": False, "error": str(exc)}

    def health(self):
        return self._call("GET", "/health")

    def scene_state(self):
        return self._call("GET", "/profile/scene_state")

    def viewport_state(self):
        return self._call("GET", "/profile/viewport_state")

    def list_cameras(self):
        return self._call("POST", "/controls/camera", {"list": True})

    def create_follow_camera(self, scene_index):
        return self._call("POST", "/controls/camera", {"createFollow": scene_index})

    def set_active_camera(self, camera_index):
        return self._call("POST", "/controls/camera", {"setActive": camera_index})

    def rebuild(self):
        return self._call("POST", "/controls/rebuild", {})


def assert_condition(condition, message):
    if not condition:
        raise AssertionError(message)


def test_viewport_focus_sync(port=40132):
    client = MotiveApiClient(port)

    print("=" * 64)
    print("Viewport Focus Sync REST Test")
    print(f"Base URL: {client.base_url}")
    print("=" * 64)

    health = client.health()
    assert_condition(health.get("ok"), f"health failed: {health}")
    print("[1] Health check: OK")

    def wait_for_cameras(max_wait_seconds=12.0):
        end_time = time.time() + max_wait_seconds
        latest = {"ok": False, "cameras": []}
        while time.time() < end_time:
            latest = client.list_cameras()
            if latest.get("ok") and latest.get("cameras"):
                return latest
            time.sleep(0.4)
        return latest

    cams = wait_for_cameras(6.0)
    cameras = cams.get("cameras", []) if cams.get("ok") else []
    print(f"[2] Cameras before prep: {len(cameras)}")

    if len(cameras) < 2:
        scene = client.scene_state()
        assert_condition(scene.get("ok"), f"scene_state fetch failed: {scene}")
        if scene.get("sceneItemCount", 0) > 0:
            create = client.create_follow_camera(0)
            if create.get("ok"):
                print("[3] Added follow camera for scene item 0")
            else:
                print(f"[3] Could not add follow camera: {create}")

    cams = wait_for_cameras(8.0)
    if not cams.get("ok") or not cams.get("cameras"):
        client.rebuild()
        time.sleep(0.3)
        cams = wait_for_cameras(8.0)
    assert_condition(cams.get("ok"), f"camera list failed after prep: {cams}")
    cameras = cams.get("cameras", [])
    assert_condition(len(cameras) >= 1, "no cameras available")
    print(f"[4] Cameras after prep: {len(cameras)}")

    # Validate first 2 cameras when available; first camera always validated.
    verify_count = min(2, len(cameras))
    for i in range(verify_count):
        cam = cameras[i]
        cam_index = cam.get("index", i)
        expected_camera_id = cam.get("id", "")

        result = client.set_active_camera(cam_index)
        assert_condition(result.get("ok"), f"setActive failed for {cam_index}: {result}")
        if expected_camera_id and result.get("activeCameraId"):
            assert_condition(
                result.get("activeCameraId") == expected_camera_id,
                f"activeCameraId mismatch for index {cam_index}: "
                f"{result.get('activeCameraId')} != {expected_camera_id}",
            )

        # Let UI/state callbacks settle.
        time.sleep(0.1)
        viewport = client.viewport_state()
        assert_condition(viewport.get("ok"), f"viewport_state fetch failed: {viewport}")

        focused_index = viewport.get("focusedViewportIndex")
        focused_camera_id = viewport.get("focusedViewportCameraId", "")
        viewport_camera_ids = viewport.get("viewportCameraIds", [])

        assert_condition(isinstance(focused_index, int), "focusedViewportIndex missing/invalid")
        assert_condition(isinstance(viewport_camera_ids, list), "viewportCameraIds missing/invalid")
        assert_condition(0 <= focused_index < len(viewport_camera_ids), "focusedViewportIndex out of range")

        slot_camera_id = viewport_camera_ids[focused_index]
        assert_condition(
            focused_camera_id == slot_camera_id,
            f"focused camera mismatch: focusedViewportCameraId={focused_camera_id}, slot={slot_camera_id}",
        )

        if expected_camera_id:
            assert_condition(
                focused_camera_id == expected_camera_id,
                f"selected camera not focused: selected={expected_camera_id}, focused={focused_camera_id}",
            )

        print(
            f"[5.{i + 1}] setActive({cam_index}) -> focusedViewportIndex={focused_index}, "
            f"focusedViewportCameraId={focused_camera_id}"
        )

    print("[6] Viewport focus sync assertions passed")
    return True


if __name__ == "__main__":
    test_port = int(sys.argv[1]) if len(sys.argv) > 1 else 40132
    try:
        ok = test_viewport_focus_sync(test_port)
    except AssertionError as exc:
        print(f"FAILED: {exc}")
        ok = False
    except Exception as exc:
        print(f"ERROR: {exc}")
        ok = False
    sys.exit(0 if ok else 1)

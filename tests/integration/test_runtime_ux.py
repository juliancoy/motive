#!/usr/bin/env python3
"""Runtime UX integration test suite for motive_editor.

This suite validates end-user critical runtime flows through the control server:
- Server startup and health
- Profile visibility endpoints
- Hierarchy/read model state surface
- Camera/selection/character control interactions
- Error handling for invalid controls
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


def pick_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def api_call(method: str, base_url: str, path: str, payload: dict | None = None, timeout: float = 20.0) -> dict:
    url = base_url + path
    headers = {"Content-Type": "application/json"}
    body = None
    if payload is not None:
        body = json.dumps(payload).encode("utf-8")
    req = Request(url, data=body, headers=headers, method=method)

    try:
        with urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8")
    except HTTPError as exc:
        raw = exc.read().decode("utf-8") if exc.fp is not None else ""
        try:
            parsed = json.loads(raw) if raw else {}
        except json.JSONDecodeError:
            parsed = {"ok": False, "error": f"HTTP {exc.code}: {exc.reason}"}
        parsed.setdefault("ok", False)
        parsed.setdefault("http_status", exc.code)
        return parsed
    except URLError as exc:
        raise RuntimeError(f"request failed {method} {path}: {exc}") from exc

    try:
        return json.loads(raw)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"non-json response for {method} {path}: {raw[:200]}...") from exc


def api_call_with_retry(
    method: str,
    base_url: str,
    path: str,
    payload: dict | None = None,
    retries: int = 3,
    timeout: float = 20.0,
) -> dict:
    last_exc = None
    for _ in range(retries):
        try:
            return api_call(method, base_url, path, payload, timeout=timeout)
        except Exception as exc:  # noqa: BLE001
            last_exc = exc
            time.sleep(0.3)
    raise RuntimeError(f"{method} {path} failed after {retries} attempts: {last_exc}") from last_exc


def api_get(base_url: str, path: str) -> dict:
    return api_call_with_retry("GET", base_url, path, None)


def api_post(base_url: str, path: str, payload: dict) -> dict:
    return api_call_with_retry("POST", base_url, path, payload)


def wait_for_health(base_url: str, timeout_s: float, proc: subprocess.Popen[bytes] | None = None) -> None:
    deadline = time.time() + timeout_s
    last = None
    while time.time() < deadline:
        if proc is not None and proc.poll() is not None:
            raise RuntimeError(f"editor exited before health check completed (exit={proc.returncode})")
        try:
            last = api_get(base_url, "/health")
            if last.get("ok") is True:
                return
        except Exception as exc:  # noqa: BLE001
            last = {"ok": False, "error": str(exc)}
        time.sleep(0.25)
    raise RuntimeError(f"editor health check timed out after {timeout_s}s; last={last}")


def wait_for_endpoint_ok(
    base_url: str,
    path: str,
    timeout_s: float,
    proc: subprocess.Popen[bytes] | None = None,
) -> None:
    deadline = time.time() + timeout_s
    last = None
    while time.time() < deadline:
        if proc is not None and proc.poll() is not None:
            raise RuntimeError(f"editor exited while waiting for {path} (exit={proc.returncode})")
        try:
            last = api_get(base_url, path)
            if last.get("ok") is True:
                return
        except Exception as exc:  # noqa: BLE001
            last = {"ok": False, "error": str(exc)}
        time.sleep(0.3)
    raise RuntimeError(f"{path} did not become ready in {timeout_s}s; last={last}")


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def post_until_ok(base_url: str, path: str, payload: dict, timeout_s: float = 10.0) -> dict:
    deadline = time.time() + timeout_s
    last = {}
    while time.time() < deadline:
        last = api_post(base_url, path, payload)
        if last.get("ok") is True:
            return last
        time.sleep(0.2)
    return last


def primitive_cull_modes(scene_item: dict) -> list[tuple[int, int, str]]:
    modes = []
    for mesh in scene_item.get("meshes", []):
        mesh_index = int(mesh.get("meshIndex", -1))
        for primitive in mesh.get("primitives", []):
            primitive_index = int(primitive.get("primitiveIndex", -1))
            cull_mode = str(primitive.get("cullMode", "back"))
            if mesh_index >= 0 and primitive_index >= 0:
                modes.append((mesh_index, primitive_index, cull_mode))
    return modes


def expect_origin_locked_plane(scene_item: dict) -> None:
    expect(scene_item.get("translation") == [0, 0, 0], f"plane indicator is not origin-centered: {scene_item}")
    expect(scene_item.get("rotation") == [0, 0, 0], f"plane indicator has unexpected rotation: {scene_item}")
    expect(scene_item.get("scale") == [1, 1, 1], f"plane indicator has unexpected scale: {scene_item}")


def run_suite(base_url: str) -> None:
    health = api_get(base_url, "/health")
    expect(health.get("ok") is True, f"/health failed: {health}")

    status = api_get(base_url, "/profile/status")
    expect(status.get("ok") is True, f"/profile/status failed: {status}")
    expect(status.get("application") == "MotiveEditor", f"unexpected application: {status}")

    # Core UX observability endpoints should be available and structured.
    for endpoint in (
        "/profile/scene_state",
        "/profile/camera_state",
        "/profile/viewport_state",
        "/profile/tps_state",
        "/profile/window",
        "/profile/ui",
        "/profile/inspector",
        "/profile/performance",
        "/profile/input_state",
        "/profile/hierarchy_state",
    ):
        resp = api_get(base_url, endpoint)
        expect(resp.get("ok") is True, f"{endpoint} failed: {resp}")

    scene = api_get(base_url, "/profile/scene_state")
    scene_items = scene.get("sceneItems", [])
    expect(isinstance(scene_items, list), f"sceneItems is not a list: {scene}")

    camera_state = api_get(base_url, "/profile/camera_state")
    direction_indicator = camera_state.get("cameraTracking", {}).get("directionIndicator", {})
    expect(isinstance(direction_indicator, dict), f"missing camera direction indicator: {camera_state}")
    expect(direction_indicator.get("visible") is True, f"camera direction indicator not visible: {direction_indicator}")
    expect(
        isinstance(direction_indicator.get("vertical"), str) and direction_indicator.get("vertical"),
        f"camera direction indicator missing vertical label: {direction_indicator}",
    )
    expect(
        isinstance(direction_indicator.get("forward"), list) and len(direction_indicator.get("forward")) == 3,
        f"camera direction indicator missing forward vector: {direction_indicator}",
    )

    hierarchy = api_get(base_url, "/hierarchy")
    expect(hierarchy.get("ok") is True, f"/hierarchy failed: {hierarchy}")
    expect("settings" in hierarchy, f"/hierarchy missing settings payload: {hierarchy}")

    # Unknown control must fail cleanly (error UX).
    unknown = api_post(base_url, "/controls/not_a_control", {})
    expect(unknown.get("ok") is False, f"unknown control should fail: {unknown}")
    expect("not found" in str(unknown.get("error", "")).lower(), f"unexpected unknown error shape: {unknown}")

    # Camera list + active camera mutation path should remain stable.
    deadline = time.time() + 20.0
    camera_list = {"ok": False}
    cameras = []
    while time.time() < deadline:
        camera_list = api_post(base_url, "/controls/camera", {"list": True})
        if camera_list.get("ok") is True:
            cameras = camera_list.get("cameras", [])
            if isinstance(cameras, list) and len(cameras) >= 1:
                break
        time.sleep(0.2)

    expect(camera_list.get("ok") is True, f"camera list failed: {camera_list}")
    if isinstance(cameras, list) and len(cameras) >= 1:
        active_idx = int(cameras[0].get("index", 0))
        set_active = api_post(base_url, "/controls/camera", {"setActive": active_idx})
        expect(set_active.get("ok") is True, f"setActive failed: {set_active}")

    # Free-fly toggle should respond with explicit mode state.
    follow_mode = api_post(base_url, "/controls/camera", {"freeFly": False})
    expect(follow_mode.get("ok") is True, f"set follow mode failed: {follow_mode}")
    expect(isinstance(follow_mode.get("freeFly"), bool), f"follow mode missing freeFly state: {follow_mode}")

    free_mode = api_post(base_url, "/controls/camera", {"freeFly": True})
    expect(free_mode.get("ok") is True, f"set freeFly failed: {free_mode}")
    expect(free_mode.get("freeFly") is True, f"freeFly mode did not stick: {free_mode}")

    pitch_clamp = api_post(
        base_url,
        "/controls/camera",
        {"navigate": 0, "pitchDelta": -200, "distance": 0, "forceFreeFly": True},
    )
    expect(pitch_clamp.get("ok") is True, f"free-fly pitch navigation failed: {pitch_clamp}")
    rotation = pitch_clamp.get("rotation", [])
    expect(isinstance(rotation, list) and len(rotation) == 3, f"pitch response missing rotation: {pitch_clamp}")
    expect(abs(float(rotation[1])) <= 85.001, f"free-fly pitch exceeded clamp: {pitch_clamp}")

    planes_on = api_post(base_url, "/controls/plane-indicators", {"enabled": True})
    expect(planes_on.get("ok") is True, f"plane indicators enable failed: {planes_on}")
    expect(planes_on.get("enabled") is True, f"plane indicators did not enable: {planes_on}")
    plane_sources = set(planes_on.get("sourcePaths", []))
    expect(
        {"planes://xy", "planes://xz", "planes://yz"}.issubset(plane_sources),
        f"plane indicators missing source paths: {planes_on}",
    )

    scene_with_planes = api_get(base_url, "/profile/scene_state")
    visible_planes = [
        item
        for item in scene_with_planes.get("sceneItems", [])
        if item.get("sourcePath") in {"planes://xy", "planes://xz", "planes://yz"} and item.get("visible") is True
    ]
    expect(len(visible_planes) == 3, f"expected three visible plane indicators: {scene_with_planes}")
    for plane in visible_planes:
        expect_origin_locked_plane(plane)

    planes_off = api_post(base_url, "/controls/plane_indicators", {"enabled": False})
    expect(planes_off.get("ok") is True, f"plane indicators disable failed: {planes_off}")
    expect(planes_off.get("enabled") is False, f"plane indicators did not disable: {planes_off}")
    scene_without_planes = api_get(base_url, "/profile/scene_state")
    hidden_planes = [
        item
        for item in scene_without_planes.get("sceneItems", [])
        if item.get("sourcePath") in {"planes://xy", "planes://xz", "planes://yz"} and item.get("visible") is False
    ]
    expect(len(hidden_planes) == 3, f"expected three hidden plane indicators: {scene_without_planes}")
    for plane in hidden_planes:
        expect_origin_locked_plane(plane)

    cull_target_index = -1
    cull_target_modes = []
    for index, item in enumerate(scene_without_planes.get("sceneItems", [])):
        modes = primitive_cull_modes(item)
        if 0 < len(modes) <= 8:
            cull_target_index = index
            cull_target_modes = modes
            break
    if cull_target_index < 0:
        for index, item in enumerate(scene_without_planes.get("sceneItems", [])):
            modes = primitive_cull_modes(item)
            if modes:
                cull_target_index = index
                cull_target_modes = modes
                break
    if cull_target_index >= 0:
        original_culls = cull_target_modes
        original_cull_modes = {cull_mode for _, _, cull_mode in original_culls}
        object_cull = api_post(base_url, "/controls/scene-item", {"sceneIndex": cull_target_index, "cullMode": "none"})
        expect(object_cull.get("ok") is True, f"object-level cull command failed: {object_cull}")
        cull_scene = api_get(base_url, "/profile/scene_state")
        cull_modes_after = primitive_cull_modes(cull_scene["sceneItems"][cull_target_index])
        expect(cull_modes_after, f"object-level cull target has no primitives after mutation: {cull_scene}")
        expect(
            all(cull_mode == "none" for _, _, cull_mode in cull_modes_after),
            f"object-level cull did not apply to every primitive: {cull_modes_after}",
        )
        if len(original_cull_modes) == 1:
            restore_cull = api_post(
                base_url,
                "/controls/scene-item",
                {"sceneIndex": cull_target_index, "cullMode": next(iter(original_cull_modes))},
            )
            expect(restore_cull.get("ok") is True, f"failed to restore object cull: {restore_cull}")
        else:
            for mesh_index, primitive_index, cull_mode in original_culls:
                restore_cull = api_post(
                    base_url,
                    "/controls/primitive",
                    {
                        "sceneIndex": cull_target_index,
                        "meshIndex": mesh_index,
                        "primitiveIndex": primitive_index,
                        "cullMode": cull_mode,
                    },
                )
                expect(restore_cull.get("ok") is True, f"failed to restore primitive cull: {restore_cull}")

    bootstrap = api_post(base_url, "/controls/bootstrap_tps", {"force": True})
    expect(bootstrap.get("ok") is True, f"bootstrap_tps failed: {bootstrap}")
    expect(isinstance(bootstrap.get("applied"), bool), f"bootstrap_tps missing applied flag: {bootstrap}")
    expect(isinstance(bootstrap.get("reasons", []), list), f"bootstrap_tps missing reasons list: {bootstrap}")

    if len(scene_items) >= 1:
        select = post_until_ok(base_url, "/controls/selection", {"sceneIndex": 0}, timeout_s=12.0)
        expect(select.get("ok") is True, f"selection failed: {select}")
        inspector = api_get(base_url, "/profile/inspector")
        expect(
            inspector.get("alignBottomToGroundVisible") is True,
            f"align-bottom-to-ground action is not visible for object selection: {inspector}",
        )
        expect(
            inspector.get("alignBottomToGroundEnabled") is True,
            f"align-bottom-to-ground action is not enabled for object selection: {inspector}",
        )
        original_scene = api_get(base_url, "/profile/scene_state")
        original_items = original_scene.get("sceneItems", [])
        original_translation = (
            original_items[0].get("translation", [])
            if isinstance(original_items, list) and len(original_items) >= 1 and isinstance(original_items[0], dict)
            else []
        )
        align = api_post(base_url, "/controls/scene-item", {"sceneIndex": 0, "alignBottomToGround": True})
        expect(align.get("ok") is True, f"align-bottom-to-ground command failed: {align}")
        expect(align.get("alignedBottomToGround") is True, f"align-bottom-to-ground flag missing: {align}")
        aligned_inspector = api_get(base_url, "/profile/inspector")
        aligned_min = aligned_inspector.get("bounds", {}).get("min", [])
        expect(
            isinstance(aligned_min, list) and len(aligned_min) == 3 and abs(float(aligned_min[1])) <= 0.02,
            f"aligned bounds bottom is not on ground plane: {aligned_inspector}",
        )
        if isinstance(original_translation, list) and len(original_translation) == 3:
            restore = api_post(base_url, "/controls/scene-item", {"sceneIndex": 0, "translation": original_translation})
            expect(restore.get("ok") is True, f"failed to restore original translation after align test: {restore}")

    # Single-owner controllable UX contract when there are at least 2 items.
    if len(scene_items) >= 2:
        first = post_until_ok(base_url, "/controls/character", {"sceneIndex": 0, "controllable": True}, timeout_s=12.0)
        expect(first.get("ok") is True, f"first controllable set failed: {first}")
        second = post_until_ok(base_url, "/controls/character", {"sceneIndex": 1, "controllable": True}, timeout_s=12.0)
        expect(second.get("ok") is True, f"second controllable set failed: {second}")

        scene_after = api_get(base_url, "/profile/scene_state")
        controllable = [bool(item.get("isControllable", False)) for item in scene_after.get("sceneItems", [])]
        expect(sum(controllable) <= 1, f"expected <=1 controllable after handoff, got: {controllable}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Runtime UX integration tests")
    parser.add_argument("--editor", default="./build/motive_editor", help="Path to motive_editor binary")
    parser.add_argument("--project-dir", default="./projects/default", help="Project directory to open")
    parser.add_argument("--port", type=int, default=0, help="Control server port (0 picks free port)")
    parser.add_argument("--startup-timeout", type=float, default=45.0, help="Seconds to wait for startup")
    parser.add_argument("--log", default="/tmp/motive_runtime_ux_editor.log", help="Editor output log path")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    editor_path = (repo_root / args.editor).resolve() if args.editor.startswith(".") else Path(args.editor).resolve()
    if not editor_path.exists():
        raise FileNotFoundError(f"Editor binary not found: {editor_path}")

    project_dir = (repo_root / args.project_dir).resolve() if args.project_dir.startswith(".") else Path(args.project_dir).resolve()
    if not project_dir.exists():
        raise FileNotFoundError(f"Project directory not found: {project_dir}")

    port = args.port if args.port > 0 else pick_free_port()
    base_url = f"http://127.0.0.1:{port}"

    env = os.environ.copy()
    env["EDITOR_CONTROL_PORT"] = str(port)
    log_path = Path(args.log)
    log_path.parent.mkdir(parents=True, exist_ok=True)

    with log_path.open("w", encoding="utf-8") as logf:
        proc = subprocess.Popen(
            [str(editor_path), str(project_dir)],
            cwd=str(repo_root),
            env=env,
            stdout=logf,
            stderr=subprocess.STDOUT,
        )

    try:
        wait_for_health(base_url, args.startup_timeout, proc=proc)
        wait_for_endpoint_ok(base_url, "/profile/scene_state", args.startup_timeout, proc=proc)
        run_suite(base_url)
        print("Runtime UX suite passed")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as exc:
        print(f"FAILED: {exc}")
        sys.exit(1)
    except Exception as exc:  # noqa: BLE001
        print(f"ERROR: {exc}")
        sys.exit(1)

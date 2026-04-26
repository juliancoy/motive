#!/usr/bin/env python3
import argparse
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path


def api_get(base_url: str, path: str) -> dict:
    out = subprocess.check_output(
        ["curl", "-sS", "--max-time", "4", base_url + path],
        text=True,
        stderr=subprocess.DEVNULL,
    )
    return json.loads(out)


def api_post(base_url: str, path: str, payload: dict) -> dict:
    out = subprocess.check_output(
        [
            "curl",
            "-sS",
            "--max-time",
            "4",
            "-H",
            "Content-Type: application/json",
            "-X",
            "POST",
            "--data-binary",
            json.dumps(payload),
            base_url + path,
        ],
        text=True,
        stderr=subprocess.DEVNULL,
    )
    return json.loads(out)


def wait_for_health(base_url: str, timeout_s: float) -> None:
    deadline = time.time() + timeout_s
    last_err = None
    while time.time() < deadline:
        try:
            health = api_get(base_url, "/health")
            if health.get("ok") is True:
                return
        except Exception as exc:  # noqa: BLE001
            last_err = exc
        time.sleep(0.25)
    raise RuntimeError(f"Control server did not become healthy in {timeout_s}s: {last_err}")


def wait_for(predicate, timeout_s: float, interval_s: float = 0.1) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if predicate():
            return
        time.sleep(interval_s)
    raise RuntimeError("Timed out waiting for condition")


def ensure_project(project_dir: Path, repo_root: Path) -> None:
    project_dir.mkdir(parents=True, exist_ok=True)
    project_id = project_dir.name
    project_file = project_dir / f"{project_id}.json"

    fbx = repo_root / "thirdpersonshooter" / "55-rp_nathan_animated_003_walking_fbx" / "rp_nathan_animated_003_walking.fbx"
    if not fbx.exists():
        raise FileNotFoundError(f"Required FBX asset not found: {fbx}")

    now_ms = int(time.time() * 1000)
    doc = {
        "projectId": project_id,
        "base": {
            "projectRoot": str(repo_root / "thirdpersonshooter"),
            "galleryPath": "",
            "selectedAssetPath": "",
            "viewportAssetPath": "",
        },
        "changes": [
            {
                "field": "projectRoot",
                "op": "set",
                "ts_ms": now_ms,
                "value": str(repo_root / "thirdpersonshooter"),
            },
            {
                "field": "sceneItems",
                "op": "set",
                "ts_ms": now_ms + 1,
                "value": [
                    {
                        "name": "rp_nathan_animated_003_walking",
                        "sourcePath": str(fbx),
                        "translation": [0, 0, 0],
                        "rotation": [-90, 0, 0],
                        "scale": [1, 1, 1],
                        "animationPlaying": True,
                        "animationLoop": True,
                        "animationSpeed": 1.0,
                        "visible": True,
                    },
                    {
                        "name": "rp_nathan_animated_003_walking_clone",
                        "sourcePath": str(fbx),
                        "translation": [2, 0, 0],
                        "rotation": [-90, 0, 0],
                        "scale": [1, 1, 1],
                        "animationPlaying": True,
                        "animationLoop": True,
                        "animationSpeed": 1.0,
                        "visible": True,
                    }
                ],
            },
        ],
    }
    project_file.write_text(json.dumps(doc, indent=2), encoding="utf-8")


def pick_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def assert_single_controllable(scene_items: list, expected_true_index: int) -> None:
    controllable = [bool(item.get("isControllable", False)) for item in scene_items]
    true_count = sum(1 for v in controllable if v)
    assert true_count <= 1, f"Expected at most one controllable scene item, got {controllable}"
    assert controllable[expected_true_index] is True, (
        f"Expected sceneIndex {expected_true_index} controllable, got {controllable}"
    )


def assert_no_controllable(scene_items: list) -> None:
    controllable = [bool(item.get("isControllable", False)) for item in scene_items]
    true_count = sum(1 for v in controllable if v)
    assert true_count == 0, f"Expected no controllable scene items after selection-only step, got {controllable}"


def main() -> int:
    parser = argparse.ArgumentParser(description="REST integration test for animation/physics controls")
    parser.add_argument("--editor", default="./build/motive_editor", help="Path to motive_editor binary")
    parser.add_argument("--project-dir", default="/tmp/motive_rest_project_small", help="Project directory")
    parser.add_argument("--port", type=int, default=0, help="Control server port (0 = auto)")
    parser.add_argument("--startup-timeout", type=float, default=30.0, help="Seconds to wait for health")
    parser.add_argument("--log", default="/tmp/motive_rest_test_editor.log", help="Editor log path")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    editor = (repo_root / args.editor).resolve() if args.editor.startswith(".") else Path(args.editor).resolve()
    if not editor.exists():
        raise FileNotFoundError(f"Editor binary not found: {editor}")

    project_dir = Path(args.project_dir)
    ensure_project(project_dir, repo_root)

    port = args.port if args.port > 0 else pick_free_port()
    base_url = f"http://127.0.0.1:{port}"

    env = os.environ.copy()
    env["EDITOR_CONTROL_PORT"] = str(port)
    log_path = Path(args.log)
    log_path.parent.mkdir(parents=True, exist_ok=True)

    with log_path.open("w", encoding="utf-8") as logf:
        proc = subprocess.Popen(
            [str(editor), str(project_dir)],
            cwd=str(repo_root),
            env=env,
            stdout=logf,
            stderr=subprocess.STDOUT,
        )

    try:
        wait_for_health(base_url, args.startup_timeout)

        unknown_control_path = api_post(base_url, "/controls/not_a_command", {})
        assert unknown_control_path.get("ok") is False, (
            f"Unknown control path should fail, got: {unknown_control_path}"
        )
        assert "not found" in str(unknown_control_path.get("error", "")).lower(), (
            f"Unexpected unknown control path response: {unknown_control_path}"
        )

        scene = api_get(base_url, "/profile/scene_state")
        assert scene.get("sceneItemCount", 0) >= 2, f"Expected at least two scene items, got: {scene.get('sceneItemCount')}"
        scene_items = scene.get("sceneItems", [])
        assert len(scene_items) >= 2, "Expected at least two scene items in sceneItems"
        scene_index = 0
        other_scene_index = 1

        # Hierarchy should be collapsed by default and selection should be API-drivable.
        inspector_before = api_get(base_url, "/profile/inspector")
        assert inspector_before.get("hierarchyExpandedCount", 0) == 0, (
            f"Hierarchy expected collapsed by default, got: {inspector_before.get('hierarchyExpandedCount')}"
        )
        selection_resp = api_post(base_url, "/controls/selection", {"sceneIndex": scene_index})
        assert selection_resp.get("selected") is True, f"Failed selecting scene item: {selection_resp}"
        assert selection_resp.get("selectedSceneIndex") == scene_index, f"Unexpected inspector selection: {selection_resp}"
        scene_after_select = api_get(base_url, "/profile/scene_state")
        assert_no_controllable(scene_after_select.get("sceneItems", []))

        # Animation baseline checks from scene profile
        assert "animationPlaying" in scene_items[scene_index], "animationPlaying missing in scene profile"
        assert "animationLoop" in scene_items[scene_index], "animationLoop missing in scene profile"
        assert "animationSpeed" in scene_items[scene_index], "animationSpeed missing in scene profile"
        follow_camera = scene_items[scene_index].get("followCamera", {})
        assert follow_camera.get("exists") is True, f"Expected follow camera info on scene item, got: {follow_camera}"

        # Character control / camera mode via REST
        character_resp = api_post(base_url, "/controls/character", {"sceneIndex": scene_index, "controllable": True})
        assert character_resp.get("sceneIndex") == scene_index, f"Unexpected character response: {character_resp}"
        assert character_resp.get("isControllable") is True, f"Character not controllable: {character_resp}"
        scene_after_first_owner = api_get(base_url, "/profile/scene_state")
        assert_single_controllable(scene_after_first_owner.get("sceneItems", []), scene_index)

        character_resp_other = api_post(
            base_url,
            "/controls/character",
            {"sceneIndex": other_scene_index, "controllable": True},
        )
        assert character_resp_other.get("sceneIndex") == other_scene_index, (
            f"Unexpected character response for second owner: {character_resp_other}"
        )
        assert character_resp_other.get("isControllable") is True, (
            f"Second character not controllable: {character_resp_other}"
        )
        scene_after_second_owner = api_get(base_url, "/profile/scene_state")
        assert_single_controllable(scene_after_second_owner.get("sceneItems", []), other_scene_index)

        camera_follow_resp = api_post(base_url, "/controls/camera", {"freeFly": False})
        assert camera_follow_resp.get("freeFly") is False, f"Camera did not enter follow mode: {camera_follow_resp}"

        camera_list = api_post(base_url, "/controls/camera", {"list": True})
        assert "cameras" in camera_list and len(camera_list["cameras"]) >= 1, f"No cameras listed: {camera_list}"

        # Verify object inspector follow-cam runtime info is populated for selected object.
        inspector_after_select = api_get(base_url, "/profile/inspector")
        follow_cam_info_text = inspector_after_select.get("followCamInfoText", "")
        assert isinstance(follow_cam_info_text, str) and "Distance:" in follow_cam_info_text, (
            f"Expected populated object follow-cam inspector text, got: {inspector_after_select}"
        )

        # Animation/Physics coupling mode controls
        coupling_set = api_post(
            base_url,
            "/controls/physics_coupling",
            {"sceneIndex": scene_index, "mode": "Kinematic"},
        )
        assert coupling_set.get("mode") == "Kinematic", f"Failed setting coupling: {coupling_set}"

        coupling_get = api_post(base_url, "/controls/physics_coupling", {"sceneIndex": scene_index})
        assert coupling_get.get("mode") == "Kinematic", f"Failed reading coupling: {coupling_get}"

        # Animation controls should apply to scene/runtime.
        animation_set = api_post(
            base_url,
            "/controls/animation",
            {"sceneIndex": scene_index, "playing": False, "loop": False, "speed": 1.7},
        )
        assert animation_set.get("sceneIndex") == scene_index, f"Failed setting animation: {animation_set}"
        wait_for(
            lambda: (
                (lambda s: (
                    s["sceneItems"][scene_index].get("animationPlaying") is False
                    and s["sceneItems"][scene_index].get("animationLoop") is False
                    and abs(float(s["sceneItems"][scene_index].get("animationSpeed", 0.0)) - 1.7) < 1e-4
                ))(api_get(base_url, "/profile/scene_state"))
            ),
            timeout_s=3.0,
        )

        # Gravity controls
        gravity_set = api_post(
            base_url,
            "/controls/physics_gravity",
            {"sceneIndex": scene_index, "useGravity": False, "gravityX": 0.0, "gravityY": -3.0, "gravityZ": 0.0},
        )
        assert gravity_set.get("useGravity") is False, f"Failed setting gravity mode: {gravity_set}"
        custom = gravity_set.get("customGravity", {})
        assert abs(float(custom.get("y", 0.0)) - (-3.0)) < 1e-5, f"Unexpected gravity payload: {gravity_set}"

        gravity_get = api_post(base_url, "/controls/physics_gravity", {"sceneIndex": scene_index})
        assert gravity_get.get("useGravity") is False, f"Failed reading gravity mode: {gravity_get}"

        # Scene item visibility toggle as a live scene mutation sanity check
        scene_toggle = api_post(base_url, "/controls/scene-item", {"sceneIndex": scene_index, "visible": False})
        assert scene_toggle.get("sceneIndex") == scene_index, f"Failed scene visibility update: {scene_toggle}"
        api_post(base_url, "/controls/scene-item", {"sceneIndex": scene_index, "visible": True})

        perf = api_get(base_url, "/profile/performance")
        assert perf.get("ok") is True, f"Performance profile endpoint failed: {perf}"

        print("PASS: REST animation/physics integration checks completed.")
        print(f"Editor log: {log_path}")
        return 0
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=4)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (AssertionError, RuntimeError, FileNotFoundError, subprocess.CalledProcessError) as exc:
        print(f"FAIL: {exc}")
        sys.exit(1)

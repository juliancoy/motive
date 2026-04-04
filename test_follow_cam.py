#!/usr/bin/env python3
"""
Follow Camera REST API Test Script

Tests the follow camera functionality via REST API:
1. List cameras
2. Create follow camera
3. Verify camera follows target
4. Test persistence (save/load)

Usage:
    python3 test_follow_cam.py [port]
    
Default port is 5050.
"""

import sys
import json
import time
import urllib.request
import urllib.error


class MotiveApiClient:
    def __init__(self, port=5050):
        self.base_url = f"http://127.0.0.1:{port}"
    
    def _call(self, method, endpoint, data=None):
        """Make HTTP request and return parsed JSON."""
        url = f"{self.base_url}{endpoint}"
        headers = {"Content-Type": "application/json"}
        
        try:
            if data:
                body = json.dumps(data).encode('utf-8')
                req = urllib.request.Request(url, data=body, headers=headers, method=method)
            else:
                req = urllib.request.Request(url, method=method)
            
            with urllib.request.urlopen(req, timeout=5) as response:
                return json.loads(response.read().decode('utf-8'))
        except urllib.error.HTTPError as e:
            return {"error": f"HTTP {e.code}: {e.reason}"}
        except Exception as e:
            return {"error": str(e)}
    
    def health(self):
        """Check server health."""
        return self._call("GET", "/health")
    
    def hierarchy(self):
        """Get scene hierarchy."""
        return self._call("GET", "/hierarchy")
    
    def scene_profile(self):
        """Get scene profile including camera position."""
        return self._call("GET", "/profile/scene")
    
    def list_cameras(self):
        """List all cameras."""
        return self._call("POST", "/controls/camera", {"list": True})
    
    def create_follow_camera(self, scene_index, distance=5.0, yaw=0.0, pitch=20.0):
        """Create a follow camera for a scene item."""
        return self._call("POST", "/controls/camera", {
            "createFollow": scene_index,
            "distance": distance,
            "yaw": yaw,
            "pitch": pitch
        })
    
    def set_active_camera(self, camera_index):
        """Set the active camera by index."""
        return self._call("POST", "/controls/camera", {"setActive": camera_index})
    
    def get_camera_position(self):
        """Get current camera position from scene profile."""
        profile = self.scene_profile()
        return profile.get("cameraPosition")


def test_follow_camera(port=5050):
    """Run follow camera tests."""
    client = MotiveApiClient(port)
    
    print("=" * 60)
    print("Follow Camera REST API Test")
    print(f"Base URL: {client.base_url}")
    print("=" * 60)
    
    # Test 1: Health check
    print("\n1. Health Check...")
    health = client.health()
    if health.get("ok"):
        print("   ✓ Server is running")
    else:
        print(f"   ✗ Server not responding: {health}")
        return False
    
    # Test 2: List current cameras
    print("\n2. Listing Current Cameras...")
    cameras = client.list_cameras()
    camera_list = cameras.get("cameras", [])
    print(f"   Found {len(camera_list)} camera(s):")
    for cam in camera_list:
        cam_type = cam.get("type", "unknown")
        name = cam.get("name", "Unnamed")
        idx = cam.get("index", -1)
        if cam_type == "follow":
            target = cam.get("followTargetIndex", -1)
            print(f"   [{idx}] {name} ({cam_type}) -> Scene {target}")
        else:
            print(f"   [{idx}] {name} ({cam_type})")
    
    # Test 3: Create follow camera
    print("\n3. Creating Follow Camera for Scene Item 0...")
    result = client.create_follow_camera(scene_index=0, distance=5.0, yaw=45.0, pitch=20.0)
    if result.get("created"):
        cam_idx = result.get("cameraIndex")
        print(f"   ✓ Follow camera created at index {cam_idx}")
    else:
        print(f"   ✗ Failed to create follow camera: {result}")
        return False
    
    # Test 4: Verify camera was created
    print("\n4. Verifying Camera Creation...")
    cameras = client.list_cameras()
    camera_list = cameras.get("cameras", [])
    follow_cams = [c for c in camera_list if c.get("type") == "follow"]
    print(f"   Total cameras: {len(camera_list)}")
    print(f"   Follow cameras: {len(follow_cams)}")
    
    # Test 5: Set follow camera as active
    print("\n5. Setting Follow Camera as Active...")
    follow_cam_idx = follow_cams[0].get("index") if follow_cams else 1
    result = client.set_active_camera(follow_cam_idx)
    print(f"   Active camera set to: {result.get('activeCamera')}")
    
    # Test 6: Poll camera position (should update if target moves)
    print("\n6. Polling Camera Position (3 seconds)...")
    positions = []
    for i in range(3):
        pos = client.get_camera_position()
        positions.append(pos)
        print(f"   T+{i}s: {pos}")
        time.sleep(1)
    
    # Test 7: Verify camera is tracking
    print("\n7. Analysis:")
    if len(positions) >= 2:
        # Check if positions are different (camera is updating)
        p1 = positions[0]
        p2 = positions[-1]
        if p1 and p2:
            dist = sum((a - b) ** 2 for a, b in zip(p1, p2)) ** 0.5
            if dist > 0.001:
                print(f"   ✓ Camera is updating (moved {dist:.4f} units)")
            else:
                print(f"   - Camera stationary (target may not be moving)")
        else:
            print("   ? Could not get position data")
    
    # Test 8: Get hierarchy and verify camera is listed
    print("\n8. Checking Hierarchy...")
    hierarchy = client.hierarchy()
    if hierarchy.get("ok"):
        items = hierarchy.get("hierarchy", [])
        cam_items = [i for i in items if i.get("type") == "Camera"]
        print(f"   Camera entries in hierarchy: {len(cam_items)}")
        for item in cam_items:
            label = item.get("label", "Unknown")
            print(f"   - {label}")
    
    print("\n" + "=" * 60)
    print("Test Complete!")
    print("=" * 60)
    print("\nTo test persistence:")
    print("1. Save the project in the editor")
    print("2. Close and reopen the editor")
    print("3. Run this script again - follow camera should still exist")
    
    return True


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 5050
    success = test_follow_camera(port)
    sys.exit(0 if success else 1)

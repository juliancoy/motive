#!/usr/bin/env python3
"""
Test script for Motive Editor Physics REST API
Tests the new physics_coupling and physics_gravity endpoints
"""

import requests
import json
import sys

BASE_URL = "http://localhost:40130"

def test_health():
    """Check if the control server is running"""
    try:
        r = requests.get(f"{BASE_URL}/health", timeout=2)
        print(f"✅ Server is running: {r.json()}")
        return True
    except Exception as e:
        print(f"❌ Server not available: {e}")
        return False

def test_scene_profile():
    """Get current scene info to see available scene items"""
    try:
        r = requests.get(f"{BASE_URL}/profile/scene", timeout=2)
        data = r.json()
        if data.get("ok"):
            print(f"\n📊 Scene Profile:")
            print(f"   Root Path: {data.get('rootPath', 'N/A')}")
            print(f"   Scene Items: {data.get('sceneItemCount', 0)}")
            
            items = data.get("sceneItems", [])
            for i, item in enumerate(items):
                print(f"   [{i}] {item.get('name', 'Unknown')}")
            return items
        else:
            print(f"❌ Failed to get scene profile: {data}")
            return []
    except Exception as e:
        print(f"❌ Error getting scene profile: {e}")
        return []

def set_physics_coupling(scene_index, mode):
    """Set animation-physics coupling mode for a scene item"""
    valid_modes = [
        "AnimationOnly", "Kinematic", "RootMotionPhysics", 
        "PhysicsDriven", "Ragdoll", "PartialRagdoll", "ActiveRagdoll"
    ]
    
    if mode not in valid_modes:
        print(f"❌ Invalid mode '{mode}'. Valid modes: {valid_modes}")
        return False
    
    try:
        r = requests.post(
            f"{BASE_URL}/controls/physics_coupling",
            json={"sceneIndex": scene_index, "mode": mode},
            timeout=2
        )
        data = r.json()
        if data.get("ok"):
            print(f"✅ Set scene[{scene_index}] coupling to: {mode}")
            return True
        else:
            print(f"❌ Failed: {data.get('error', 'Unknown error')}")
            return False
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

def get_physics_coupling(scene_index):
    """Get current coupling mode for a scene item"""
    try:
        r = requests.post(
            f"{BASE_URL}/controls/physics_coupling",
            json={"sceneIndex": scene_index},
            timeout=2
        )
        data = r.json()
        if data.get("ok"):
            print(f"📋 Scene[{scene_index}] coupling: {data.get('mode', 'Unknown')}")
            return data.get("mode")
        else:
            print(f"❌ Failed: {data.get('error', 'Unknown error')}")
            return None
    except Exception as e:
        print(f"❌ Error: {e}")
        return None

def set_physics_gravity(scene_index, use_gravity, custom_gravity=None):
    """Set gravity settings for a scene item"""
    payload = {
        "sceneIndex": scene_index,
        "useGravity": use_gravity
    }
    
    if custom_gravity:
        payload["gravityX"] = custom_gravity.get("x", 0)
        payload["gravityY"] = custom_gravity.get("y", 0)
        payload["gravityZ"] = custom_gravity.get("z", 0)
    
    try:
        r = requests.post(
            f"{BASE_URL}/controls/physics_gravity",
            json=payload,
            timeout=2
        )
        data = r.json()
        if data.get("ok"):
            cg = data.get("customGravity", {})
            print(f"✅ Set scene[{scene_index}] gravity:")
            print(f"   useGravity: {data.get('useGravity')}")
            print(f"   customGravity: ({cg.get('x')}, {cg.get('y')}, {cg.get('z')})")
            return True
        else:
            print(f"❌ Failed: {data.get('error', 'Unknown error')}")
            return False
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

def get_physics_gravity(scene_index):
    """Get current gravity settings for a scene item"""
    try:
        r = requests.post(
            f"{BASE_URL}/controls/physics_gravity",
            json={"sceneIndex": scene_index},
            timeout=2
        )
        data = r.json()
        if data.get("ok"):
            cg = data.get("customGravity", {})
            print(f"📋 Scene[{scene_index}] gravity:")
            print(f"   useGravity: {data.get('useGravity')}")
            print(f"   customGravity: ({cg.get('x')}, {cg.get('y')}, {cg.get('z')})")
            return data
        else:
            print(f"❌ Failed: {data.get('error', 'Unknown error')}")
            return None
    except Exception as e:
        print(f"❌ Error: {e}")
        return None

def demo_all_modes(scene_index):
    """Demo: Cycle through all coupling modes"""
    modes = [
        "AnimationOnly",
        "Kinematic", 
        "RootMotionPhysics",
        "PhysicsDriven",
        "Ragdoll",
        "PartialRagdoll",
        "ActiveRagdoll"
    ]
    
    print(f"\n🎬 Demo: Cycling through all coupling modes for scene[{scene_index}]")
    for mode in modes:
        set_physics_coupling(scene_index, mode)

def demo_gravity_settings(scene_index):
    """Demo: Various gravity configurations"""
    print(f"\n🎬 Demo: Testing gravity settings for scene[{scene_index}]")
    
    # Normal gravity
    print("\n1. Normal world gravity:")
    set_physics_gravity(scene_index, True)
    
    # No gravity (floating)
    print("\n2. No gravity (floating):")
    set_physics_gravity(scene_index, False)
    
    # Weak gravity
    print("\n3. Weak gravity (moon-like):")
    set_physics_gravity(scene_index, True, {"x": 0, "y": -1.6, "z": 0})
    
    # Upward gravity (buoyancy)
    print("\n4. Upward gravity (buoyancy):")
    set_physics_gravity(scene_index, True, {"x": 0, "y": 5.0, "z": 0})
    
    # Sideways gravity
    print("\n5. Sideways gravity:")
    set_physics_gravity(scene_index, True, {"x": 5.0, "y": 0, "z": 0})

def main():
    print("=" * 60)
    print("Motive Editor Physics API Test Script")
    print("=" * 60)
    
    # Check server
    if not test_health():
        print("\nMake sure motive_editor is running with --control-port 40130")
        sys.exit(1)
    
    # Get scene info
    items = test_scene_profile()
    if not items:
        print("\n⚠️ No scene items found. Load a model first.")
        sys.exit(1)
    
    # Use first item for demos
    test_index = 0
    print(f"\n🎯 Using scene[{test_index}] '{items[test_index].get('name', 'Unknown')}' for tests")
    
    # Show current state
    print("\n" + "=" * 60)
    print("CURRENT STATE")
    print("=" * 60)
    get_physics_coupling(test_index)
    get_physics_gravity(test_index)
    
    # Run demos if user wants
    print("\n" + "=" * 60)
    print("AVAILABLE TESTS")
    print("=" * 60)
    print("1. Test all coupling modes")
    print("2. Test gravity settings")
    print("3. Custom: Set specific coupling mode")
    print("4. Custom: Set specific gravity")
    print("5. Exit")
    
    try:
        choice = input("\nEnter choice (1-5): ").strip()
        
        if choice == "1":
            demo_all_modes(test_index)
        elif choice == "2":
            demo_gravity_settings(test_index)
        elif choice == "3":
            mode = input("Enter mode (AnimationOnly/Kinematic/RootMotionPhysics/PhysicsDriven/Ragdoll/PartialRagdoll/ActiveRagdoll): ").strip()
            set_physics_coupling(test_index, mode)
        elif choice == "4":
            use_grav = input("Use gravity? (y/n): ").strip().lower() == "y"
            if use_grav:
                gx = float(input("Gravity X (0): ") or "0")
                gy = float(input("Gravity Y (-9.81): ") or "-9.81")
                gz = float(input("Gravity Z (0): ") or "0")
                set_physics_gravity(test_index, True, {"x": gx, "y": gy, "z": gz})
            else:
                set_physics_gravity(test_index, False)
        else:
            print("Exiting...")
            
    except KeyboardInterrupt:
        print("\n\nExiting...")
    except Exception as e:
        print(f"\nError: {e}")
    
    print("\n" + "=" * 60)
    print("FINAL STATE")
    print("=" * 60)
    get_physics_coupling(test_index)
    get_physics_gravity(test_index)

if __name__ == "__main__":
    main()

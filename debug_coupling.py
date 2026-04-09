#!/usr/bin/env python3
"""Debug script to trace the physics coupling issue"""

import requests
import json

BASE = "http://localhost:40132"

def check_dropdown_options():
    """Check what options are actually in the dropdown via scene profile"""
    r = requests.get(f"{BASE}/profile/scene", timeout=2)
    data = r.json()
    
    print("=== SCENE PROFILE ===")
    print(f"Scene items: {data.get('sceneItemCount', 0)}")
    
    for i, item in enumerate(data.get('sceneItems', [])):
        print(f"\nItem [{i}]: {item.get('name', 'Unknown')}")
        print(f"  Coupling: {item.get('animationPhysicsCoupling', 'NOT SET')}")
        print(f"  Use Gravity: {item.get('useGravity', 'NOT SET')}")
        print(f"  Custom Gravity: {item.get('customGravity', 'NOT SET')}")
    
    return data.get('sceneItems', [])

def test_coupling_api():
    """Test the coupling API directly"""
    print("\n=== TESTING COUPLING API ===")
    
    # Get current
    r = requests.post(f"{BASE}/controls/physics_coupling", 
        json={"sceneIndex": 0}, timeout=2)
    print(f"Current coupling: {r.json()}")
    
    # Set to PhysicsDriven
    print("\nSetting to PhysicsDriven...")
    r = requests.post(f"{BASE}/controls/physics_coupling",
        json={"sceneIndex": 0, "mode": "PhysicsDriven"}, timeout=2)
    result = r.json()
    print(f"Response: {result}")
    
    if result.get('ok'):
        print("✅ API reports success")
        # Verify it was set
        r = requests.post(f"{BASE}/controls/physics_coupling",
            json={"sceneIndex": 0}, timeout=2)
        verify = r.json()
        print(f"Verification: {verify}")
        
        if verify.get('mode') == 'PhysicsDriven':
            print("✅ Value persisted correctly")
        else:
            print(f"❌ Value NOT persisted! Expected 'PhysicsDriven', got '{verify.get('mode')}'")
    else:
        print(f"❌ API error: {result.get('error')}")

def test_all_modes():
    """Test all coupling modes"""
    modes = ["AnimationOnly", "Kinematic", "RootMotionPhysics", 
             "PhysicsDriven", "Ragdoll", "PartialRagdoll", "ActiveRagdoll"]
    
    print("\n=== TESTING ALL MODES ===")
    
    for mode in modes:
        # Set mode
        r = requests.post(f"{BASE}/controls/physics_coupling",
            json={"sceneIndex": 0, "mode": mode}, timeout=2)
        result = r.json()
        
        # Verify
        r = requests.post(f"{BASE}/controls/physics_coupling",
            json={"sceneIndex": 0}, timeout=2)
        verify = r.json()
        
        actual = verify.get('mode', 'ERROR')
        status = "✅" if actual == mode else "❌"
        print(f"{status} Set: {mode:20} | Actual: {actual}")

def check_hierarchy():
    """Check hierarchy endpoint"""
    r = requests.get(f"{BASE}/hierarchy", timeout=2)
    data = r.json()
    print("\n=== HIERARCHY ===")
    print(json.dumps(data, indent=2))

def main():
    print("=" * 60)
    print("PHYSICS COUPLING DEBUG SCRIPT")
    print("=" * 60)
    
    # Check server
    try:
        r = requests.get(f"{BASE}/health", timeout=2)
        print(f"✅ Server running: {r.json()}")
    except Exception as e:
        print(f"❌ Server not running: {e}")
        return
    
    # Check scene
    items = check_dropdown_options()
    if not items:
        print("\n⚠️ No scene items! Load a model first.")
        return
    
    # Test API
    test_coupling_api()
    
    # Test all modes
    test_all_modes()
    
    # Final check
    print("\n=== FINAL STATE ===")
    check_dropdown_options()

if __name__ == "__main__":
    main()

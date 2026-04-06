#!/usr/bin/env python3
"""Quick test of physics API - run this after starting motive_editor"""

import requests
import sys

BASE = "http://localhost:40130"

def test():
    # Check health
    try:
        r = requests.get(f"{BASE}/health", timeout=2)
        print(f"✅ Server OK: {r.json()}")
    except Exception as e:
        print(f"❌ Server not running: {e}")
        print("Start motive_editor first: ./motive_editor --control-port 40130")
        sys.exit(1)
    
    # Get scene items
    r = requests.get(f"{BASE}/profile/scene", timeout=2)
    items = r.json().get("sceneItems", [])
    print(f"\n📊 Found {len(items)} scene items:")
    for i, item in enumerate(items):
        print(f"   [{i}] {item.get('name', 'Unknown')}")
    
    if not items:
        print("Load a model first!")
        return
    
    idx = 0  # Test with first item
    name = items[idx].get('name', 'Unknown')
    print(f"\n🎯 Testing with [{idx}] {name}")
    
    # Test 1: Get current coupling
    print("\n1️⃣ Getting current coupling mode...")
    r = requests.post(f"{BASE}/controls/physics_coupling", json={"sceneIndex": idx}, timeout=2)
    print(f"   Current: {r.json().get('mode', 'N/A')}")
    
    # Test 2: Set to PhysicsDriven
    print("\n2️⃣ Setting to PhysicsDriven...")
    r = requests.post(f"{BASE}/controls/physics_coupling", 
        json={"sceneIndex": idx, "mode": "PhysicsDriven"}, timeout=2)
    print(f"   Result: {r.json().get('mode', 'Failed')}")
    
    # Test 3: Set to Ragdoll
    print("\n3️⃣ Setting to Ragdoll...")
    r = requests.post(f"{BASE}/controls/physics_coupling",
        json={"sceneIndex": idx, "mode": "Ragdoll"}, timeout=2)
    print(f"   Result: {r.json().get('mode', 'Failed')}")
    
    # Test 4: Get current gravity
    print("\n4️⃣ Getting current gravity settings...")
    r = requests.post(f"{BASE}/controls/physics_gravity", json={"sceneIndex": idx}, timeout=2)
    data = r.json()
    cg = data.get("customGravity", {})
    print(f"   useGravity: {data.get('useGravity')}")
    print(f"   customGravity: ({cg.get('x')}, {cg.get('y')}, {cg.get('z')})")
    
    # Test 5: Disable gravity (floating)
    print("\n5️⃣ Disabling gravity (floating)...")
    r = requests.post(f"{BASE}/controls/physics_gravity",
        json={"sceneIndex": idx, "useGravity": False}, timeout=2)
    print(f"   Result: useGravity={r.json().get('useGravity')}")
    
    # Test 6: Custom upward gravity (buoyancy)
    print("\n6️⃣ Setting upward gravity (buoyancy)...")
    r = requests.post(f"{BASE}/controls/physics_gravity",
        json={"sceneIndex": idx, "useGravity": True, "gravityX": 0, "gravityY": 5.0, "gravityZ": 0}, timeout=2)
    data = r.json()
    cg = data.get("customGravity", {})
    print(f"   Result: useGravity={data.get('useGravity')}, gravity=({cg.get('x')}, {cg.get('y')}, {cg.get('z')})")
    
    # Test 7: Reset to normal
    print("\n7️⃣ Resetting to normal gravity...")
    r = requests.post(f"{BASE}/controls/physics_gravity",
        json={"sceneIndex": idx, "useGravity": True, "gravityX": 0, "gravityY": 0, "gravityZ": 0}, timeout=2)
    print(f"   Result: useGravity={r.json().get('useGravity')}")
    
    # Reset coupling to AnimationOnly
    print("\n8️⃣ Resetting coupling to AnimationOnly...")
    r = requests.post(f"{BASE}/controls/physics_coupling",
        json={"sceneIndex": idx, "mode": "AnimationOnly"}, timeout=2)
    print(f"   Result: {r.json().get('mode', 'Failed')}")
    
    print("\n✅ All tests completed!")

if __name__ == "__main__":
    test()

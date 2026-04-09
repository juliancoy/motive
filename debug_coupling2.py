#!/usr/bin/env python3
"""Debug script with more detailed error checking"""

import requests
import json

BASE = "http://localhost:40132"

def test_direct():
    """Test with direct HTTP to see raw response"""
    import http.client
    
    print("=== DIRECT HTTP TEST ===")
    
    conn = http.client.HTTPConnection("localhost", 40132)
    
    # Test GET /health
    print("\n1. Testing GET /health...")
    conn.request("GET", "/health")
    r = conn.getresponse()
    print(f"Status: {r.status}")
    print(f"Response: {r.read().decode()}")
    
    # Test POST /controls/physics_coupling
    print("\n2. Testing POST /controls/physics_coupling...")
    payload = json.dumps({"sceneIndex": 0, "mode": "PhysicsDriven"})
    headers = {"Content-Type": "application/json"}
    conn.request("POST", "/controls/physics_coupling", body=payload, headers=headers)
    r = conn.getresponse()
    print(f"Status: {r.status}")
    print(f"Response: {r.read().decode()}")
    
    conn.close()

def test_step_by_step():
    """Test step by step to isolate the issue"""
    print("\n=== STEP BY STEP TEST ===")
    
    # Step 1: Get scene items
    print("\n1. Getting scene items...")
    r = requests.get(f"{BASE}/profile/scene", timeout=2)
    data = r.json()
    if not data.get('ok'):
        print(f"❌ Failed to get scene: {data}")
        return
    
    items = data.get('sceneItems', [])
    print(f"   Found {len(items)} items")
    if not items:
        print("   No items to test with!")
        return
    
    # Step 2: Test GET mode (no mode in body)
    print("\n2. Testing GET current mode...")
    r = requests.post(f"{BASE}/controls/physics_coupling",
        json={"sceneIndex": 0}, timeout=2)
    print(f"   Response: {r.json()}")
    
    # Step 3: Test SET mode
    print("\n3. Testing SET mode...")
    r = requests.post(f"{BASE}/controls/physics_coupling",
        json={"sceneIndex": 0, "mode": "PhysicsDriven"}, timeout=2)
    result = r.json()
    print(f"   Response: {result}")
    
    if not result.get('ok'):
        print(f"   ❌ SET failed: {result.get('error')}")
    else:
        print(f"   ✅ SET succeeded")
        
        # Step 4: Verify
        print("\n4. Verifying...")
        r = requests.post(f"{BASE}/controls/physics_coupling",
            json={"sceneIndex": 0}, timeout=2)
        verify = r.json()
        print(f"   Response: {verify}")
        
        if verify.get('mode') == 'PhysicsDriven':
            print("   ✅ Verified!")
        else:
            print(f"   ❌ Verification failed! Expected 'PhysicsDriven', got '{verify.get('mode')}'")

def check_ui_state():
    """Check what the UI thinks the state is"""
    print("\n=== UI STATE CHECK ===")
    
    # Get hierarchy
    r = requests.get(f"{BASE}/hierarchy", timeout=2)
    data = r.json()
    print(f"Hierarchy: {json.dumps(data, indent=2)[:500]}...")

def main():
    print("=" * 60)
    print("PHYSICS COUPLING DEBUG v2")
    print("=" * 60)
    
    test_direct()
    test_step_by_step()
    check_ui_state()

if __name__ == "__main__":
    main()

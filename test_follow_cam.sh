#!/bin/bash

# Follow Camera REST API Test Script
# Usage: ./test_follow_cam.sh [port]
# Default port is 40132

PORT=${1:-40132}
BASE_URL="http://127.0.0.1:$PORT"

echo "========================================="
echo "Follow Camera REST API Test"
echo "Base URL: $BASE_URL"
echo "========================================="

# Helper function for API calls
call_api() {
    local method=$1
    local endpoint=$2
    local body=$3
    
    if [ -n "$body" ]; then
        curl -s -X "$method" "$BASE_URL$endpoint" \
            -H "Content-Type: application/json" \
            -d "$body"
    else
        curl -s -X "$method" "$BASE_URL$endpoint"
    fi
}

echo ""
echo "1. Checking server health..."
call_api "GET" "/health" | python3 -m json.tool 2>/dev/null || echo "Server not running on port $PORT"

echo ""
echo "2. Getting hierarchy..."
call_api "GET" "/hierarchy" | python3 -m json.tool 2>/dev/null | head -50

echo ""
echo "3. Getting scene profile (includes cameras)..."
call_api "GET" "/profile/scene" | python3 -m json.tool 2>/dev/null | head -80

echo ""
echo "4. Listing current cameras..."
call_api "POST" "/controls/camera" '{"list": true}' | python3 -m json.tool 2>/dev/null

echo ""
echo "5. Creating follow camera for scene item 0..."
call_api "POST" "/controls/camera" '{"createFollow": 0, "distance": 5.0, "yaw": 45.0, "pitch": 20.0}' | python3 -m json.tool 2>/dev/null

echo ""
echo "6. Listing cameras again (should show new follow camera)..."
call_api "POST" "/controls/camera" '{"list": true}' | python3 -m json.tool 2>/dev/null

echo ""
echo "7. Setting camera 1 as active (the follow camera)..."
call_api "POST" "/controls/camera" '{"setActive": 1}' | python3 -m json.tool 2>/dev/null

echo ""
echo "8. Getting camera position (should update as object moves)..."
for i in {1..3}; do
    echo "   Poll $i:"
    call_api "GET" "/profile/scene" | python3 -c "import json,sys; d=json.load(sys.stdin); print('   Camera pos:', d.get('cameraPosition', 'N/A'))"
    sleep 1
done

echo ""
echo "9. Test complete!"
echo "========================================="

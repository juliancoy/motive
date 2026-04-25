#include "../../jolt/UnitTests/doctest.h"
#include <string>
#include <map>
#include <vector>
#include <cmath>

// Mock ControlServer for testing controller logic
class MockControlServer {
public:
    struct FollowCameraRequest {
        int sceneIndex = -1;
        float distance = 5.0f;
        float yaw = 0.0f;
        float pitch = 20.0f;
        float smoothSpeed = 5.0f;
    };
    
    struct CameraTransform {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float rx = 0.0f;
        float ry = 0.0f;
        float rz = 0.0f;
    };
    
    enum ResponseCode {
        Success = 200,
        BadRequest = 400,
        NotFound = 404,
        ServerError = 500
    };
    
    struct Response {
        ResponseCode code = Success;
        std::string message;
        std::map<std::string, std::string> data;
    };
    
    Response handleCreateFollowCamera(const FollowCameraRequest& request) {
        Response response;
        
        // Validate request
        if (request.sceneIndex < 0) {
            response.code = BadRequest;
            response.message = "Invalid scene index";
            return response;
        }
        
        if (request.distance <= 0.0f) {
            response.code = BadRequest;
            response.message = "Distance must be positive";
            return response;
        }
        
        if (request.pitch < -90.0f || request.pitch > 90.0f) {
            response.code = BadRequest;
            response.message = "Pitch must be between -90 and 90 degrees";
            return response;
        }
        
        // Convert degrees to radians for internal use
        float yawRad = request.yaw * M_PI / 180.0f;
        float pitchRad = request.pitch * M_PI / 180.0f;
        
        // Store the request
        lastFollowRequest = request;
        followCameras[request.sceneIndex] = request;
        
        response.code = Success;
        response.message = "Follow camera created";
        response.data["sceneIndex"] = std::to_string(request.sceneIndex);
        response.data["distance"] = std::to_string(request.distance);
        response.data["yaw"] = std::to_string(request.yaw);
        response.data["pitch"] = std::to_string(request.pitch);
        
        return response;
    }
    
    Response handleUpdateCameraTransform(int cameraIndex, const CameraTransform& transform) {
        Response response;
        
        if (cameraIndex < 0 || cameraIndex >= 100) { // Arbitrary limit
            response.code = NotFound;
            response.message = "Camera not found";
            return response;
        }
        
        // For follow cameras, translation/rotation should not be directly settable
        // They're computed from follow parameters
        if (followCameras.find(cameraIndex) != followCameras.end()) {
            response.code = BadRequest;
            response.message = "Cannot set transform on follow camera - use follow parameters";
            return response;
        }
        
        // For free cameras, update the transform
        cameraTransforms[cameraIndex] = transform;
        
        response.code = Success;
        response.message = "Camera transform updated";
        return response;
    }
    
    Response handleGetCameraInfo(int cameraIndex) {
        Response response;
        
        auto followIt = followCameras.find(cameraIndex);
        if (followIt != followCameras.end()) {
            // It's a follow camera
            response.code = Success;
            response.message = "Follow camera";
            response.data["type"] = "follow";
            response.data["sceneIndex"] = std::to_string(followIt->second.sceneIndex);
            response.data["distance"] = std::to_string(followIt->second.distance);
            response.data["yaw"] = std::to_string(followIt->second.yaw);
            response.data["pitch"] = std::to_string(followIt->second.pitch);
            response.data["smoothSpeed"] = std::to_string(followIt->second.smoothSpeed);
        } else {
            auto transformIt = cameraTransforms.find(cameraIndex);
            if (transformIt != cameraTransforms.end()) {
                // It's a free camera
                response.code = Success;
                response.message = "Free camera";
                response.data["type"] = "free";
                response.data["x"] = std::to_string(transformIt->second.x);
                response.data["y"] = std::to_string(transformIt->second.y);
                response.data["z"] = std::to_string(transformIt->second.z);
                response.data["rx"] = std::to_string(transformIt->second.rx);
                response.data["ry"] = std::to_string(transformIt->second.ry);
                response.data["rz"] = std::to_string(transformIt->second.rz);
            } else {
                response.code = NotFound;
                response.message = "Camera not found";
            }
        }
        
        return response;
    }
    
    // Test helper methods
    bool hasFollowCamera(int sceneIndex) const {
        return followCameras.find(sceneIndex) != followCameras.end();
    }
    
    FollowCameraRequest getLastFollowRequest() const {
        return lastFollowRequest;
    }
    
private:
    std::map<int, FollowCameraRequest> followCameras;
    std::map<int, CameraTransform> cameraTransforms;
    FollowCameraRequest lastFollowRequest;
};

TEST_SUITE("Control Server - MVC Controller Layer") {
    TEST_CASE("Create follow camera validates parameters correctly") {
        MockControlServer server;
        
        // Valid request
        MockControlServer::FollowCameraRequest validRequest;
        validRequest.sceneIndex = 5;
        validRequest.distance = 10.0f;
        validRequest.yaw = 45.0f;
        validRequest.pitch = 30.0f;
        validRequest.smoothSpeed = 3.0f;
        
        auto response = server.handleCreateFollowCamera(validRequest);
        CHECK(response.code == MockControlServer::Success);
        CHECK(server.hasFollowCamera(5) == true);
        
        // Invalid scene index
        MockControlServer::FollowCameraRequest invalidScene;
        invalidScene.sceneIndex = -1;
        response = server.handleCreateFollowCamera(invalidScene);
        CHECK(response.code == MockControlServer::BadRequest);
        
        // Invalid distance
        MockControlServer::FollowCameraRequest invalidDistance;
        invalidDistance.sceneIndex = 6;
        invalidDistance.distance = 0.0f;
        response = server.handleCreateFollowCamera(invalidDistance);
        CHECK(response.code == MockControlServer::BadRequest);
        
        // Invalid pitch
        MockControlServer::FollowCameraRequest invalidPitch;
        invalidPitch.sceneIndex = 7;
        invalidPitch.pitch = 100.0f; // Outside [-90, 90]
        response = server.handleCreateFollowCamera(invalidPitch);
        CHECK(response.code == MockControlServer::BadRequest);
    }
    
    TEST_CASE("Follow camera cannot have transform set directly") {
        MockControlServer server;
        
        // Create a follow camera
        MockControlServer::FollowCameraRequest followReq;
        followReq.sceneIndex = 5;
        server.handleCreateFollowCamera(followReq);
        
        // Try to set transform on follow camera (should fail)
        MockControlServer::CameraTransform transform;
        transform.x = 10.0f;
        auto response = server.handleUpdateCameraTransform(5, transform);
        
        CHECK(response.code == MockControlServer::BadRequest);
        CHECK(response.message.find("Cannot set transform on follow camera") != std::string::npos);
    }
    
    TEST_CASE("Free camera can have transform set") {
        MockControlServer server;
        
        // Set transform on free camera (should succeed)
        MockControlServer::CameraTransform transform;
        transform.x = 10.0f;
        transform.y = 5.0f;
        transform.z = 3.0f;
        transform.rx = 45.0f;
        
        auto response = server.handleUpdateCameraTransform(0, transform);
        CHECK(response.code == MockControlServer::Success);
        
        // Get camera info should return transform
        response = server.handleGetCameraInfo(0);
        CHECK(response.code == MockControlServer::Success);
        CHECK(response.data["type"] == "free");
        CHECK(response.data["x"] == "10.000000");
    }
    
    TEST_CASE("Get camera info returns correct data for follow cameras") {
        MockControlServer server;
        
        // Create follow camera
        MockControlServer::FollowCameraRequest request;
        request.sceneIndex = 5;
        request.distance = 8.0f;
        request.yaw = 90.0f;
        request.pitch = 25.0f;
        request.smoothSpeed = 2.5f;
        
        server.handleCreateFollowCamera(request);
        
        // Get info
        auto response = server.handleGetCameraInfo(5);
        CHECK(response.code == MockControlServer::Success);
        CHECK(response.data["type"] == "follow");
        CHECK(response.data["sceneIndex"] == "5");
        CHECK(response.data["distance"] == "8.000000");
        CHECK(response.data["yaw"] == "90.000000");
        CHECK(response.data["pitch"] == "25.000000");
        CHECK(response.data["smoothSpeed"] == "2.500000");
    }
    
    TEST_CASE("Controller enforces MVC separation") {
        // This test verifies the controller properly separates concerns:
        // 1. Validates input (controller responsibility)
        // 2. Calls appropriate model methods
        // 3. Returns appropriate view data
        
        MockControlServer server;
        
        // Test 1: Input validation
        MockControlServer::FollowCameraRequest badRequest;
        badRequest.sceneIndex = -5; // Invalid
        auto response = server.handleCreateFollowCamera(badRequest);
        CHECK(response.code == MockControlServer::BadRequest); // Controller validates
        
        // Test 2: Model interaction
        MockControlServer::FollowCameraRequest goodRequest;
        goodRequest.sceneIndex = 10;
        goodRequest.distance = 7.0f;
        response = server.handleCreateFollowCamera(goodRequest);
        CHECK(response.code == MockControlServer::Success);
        CHECK(server.hasFollowCamera(10) == true); // Model was updated
        
        // Test 3: View data formatting
        response = server.handleGetCameraInfo(10);
        CHECK(response.data.find("type") != response.data.end()); // View data formatted
        CHECK(response.data.find("distance") != response.data.end());
        CHECK(response.data.find("yaw") != response.data.end());
        CHECK(response.data.find("pitch") != response.data.end());
        // Note: transform (x,y,z,rx,ry,rz) should NOT be in follow camera response
        CHECK(response.data.find("x") == response.data.end());
        CHECK(response.data.find("y") == response.data.end());
        CHECK(response.data.find("z") == response.data.end());
    }
}
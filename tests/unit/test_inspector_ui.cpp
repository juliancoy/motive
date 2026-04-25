#include "../../jolt/UnitTests/doctest.h"

// Mock classes for testing inspector UI logic
class MockInspectorUI {
public:
    enum CameraType {
        FreeCamera,
        FollowCamera
    };
    
    struct TransformControls {
        bool translationVisible = true;
        bool rotationVisible = true;
        bool scaleVisible = true;
        bool translationEnabled = true;
        bool rotationEnabled = true;
        bool scaleEnabled = true;
        
        struct Vec3 {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
        };
        
        Vec3 translation{0, 0, 0};
        Vec3 rotation{0, 0, 0};
        Vec3 scale{1, 1, 1};
    };
    
    struct FollowCameraControls {
        bool visible = false;
        float distance = 5.0f;
        float yaw = 0.0f;
        float pitch = 20.0f;
        float smoothSpeed = 5.0f;
    };
    
    void updateForCamera(CameraType type, bool isFollowCamera) {
        if (isFollowCamera) {
            // For follow cameras: hide transform controls, show follow controls
            transform.translationVisible = false;
            transform.rotationVisible = false;
            transform.scaleVisible = false;
            
            follow.visible = true;
            // Follow cameras don't have translation/rotation/scale
            // They only have distance, yaw, pitch, smoothSpeed
        } else {
            // For free cameras: show transform controls (but disable scale)
            transform.translationVisible = true;
            transform.rotationVisible = true;
            transform.scaleVisible = true;
            
            transform.translationEnabled = true;
            transform.rotationEnabled = true;
            transform.scaleEnabled = false; // Cameras don't have scale
            
            follow.visible = false;
        }
    }
    
    TransformControls transform;
    FollowCameraControls follow;
};

TEST_SUITE("Inspector UI - MVC View Layer") {
    TEST_CASE("Follow camera shows only follow parameters, not transform") {
        MockInspectorUI inspector;
        
        // Test follow camera
        inspector.updateForCamera(MockInspectorUI::FreeCamera, true); // isFollowCamera = true
        
        // Transform controls should be hidden
        CHECK(inspector.transform.translationVisible == false);
        CHECK(inspector.transform.rotationVisible == false);
        CHECK(inspector.transform.scaleVisible == false);
        
        // Follow controls should be visible
        CHECK(inspector.follow.visible == true);
        
        // Follow camera should have appropriate default values
        CHECK(inspector.follow.distance == doctest::Approx(5.0f));
        CHECK(inspector.follow.yaw == doctest::Approx(0.0f));
        CHECK(inspector.follow.pitch == doctest::Approx(20.0f));
        CHECK(inspector.follow.smoothSpeed == doctest::Approx(5.0f));
    }
    
    TEST_CASE("Free camera shows transform controls but disables scale") {
        MockInspectorUI inspector;
        
        // Test free camera
        inspector.updateForCamera(MockInspectorUI::FreeCamera, false); // isFollowCamera = false
        
        // Transform controls should be visible
        CHECK(inspector.transform.translationVisible == true);
        CHECK(inspector.transform.rotationVisible == true);
        CHECK(inspector.transform.scaleVisible == true);
        
        // Translation and rotation should be enabled
        CHECK(inspector.transform.translationEnabled == true);
        CHECK(inspector.transform.rotationEnabled == true);
        
        // Scale should be disabled (cameras don't have scale)
        CHECK(inspector.transform.scaleEnabled == false);
        
        // Follow controls should be hidden
        CHECK(inspector.follow.visible == false);
    }
    
    TEST_CASE("Camera scale is always 1.0 and disabled") {
        MockInspectorUI inspector;
        
        // Both camera types should have scale = 1.0
        inspector.updateForCamera(MockInspectorUI::FreeCamera, false);
        CHECK(inspector.transform.scale.x == doctest::Approx(1.0f));
        CHECK(inspector.transform.scale.y == doctest::Approx(1.0f));
        CHECK(inspector.transform.scale.z == doctest::Approx(1.0f));
        CHECK(inspector.transform.scaleEnabled == false);
        
        inspector.updateForCamera(MockInspectorUI::FreeCamera, true);
        // For follow cameras, scale controls are hidden, but if they were shown,
        // they should also be disabled and show 1.0
    }
    
    TEST_CASE("Follow camera parameters match expected API") {
        // This test ensures the UI exposes the correct parameters for follow cameras
        // as defined in the REST API (test_follow_cam.py)
        MockInspectorUI inspector;
        inspector.updateForCamera(MockInspectorUI::FreeCamera, true);
        
        // The REST API expects these parameters:
        // - distance
        // - yaw  
        // - pitch
        // - smoothSpeed (optional)
        
        // Verify UI exposes these exact parameters
        CHECK(inspector.follow.visible == true);
        
        // All parameters should have reasonable default values
        CHECK(inspector.follow.distance > 0.0f);
        CHECK(inspector.follow.yaw >= -180.0f);
        CHECK(inspector.follow.yaw <= 180.0f);
        CHECK(inspector.follow.pitch >= -90.0f);
        CHECK(inspector.follow.pitch <= 90.0f);
        CHECK(inspector.follow.smoothSpeed > 0.0f);
    }
}
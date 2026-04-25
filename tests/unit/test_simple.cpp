#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "../../jolt/UnitTests/doctest.h"

// Simple test to verify the test framework works
TEST_SUITE("Simple Framework Test") {
    TEST_CASE("Test framework is working") {
        CHECK(1 + 1 == 2);
        CHECK(true == true);
        CHECK(false == false);
    }
    
    TEST_CASE("Follow camera concept test") {
        // Test the core concept: follow cameras have distance/yaw/pitch, not translation/rotation/scale
        struct FollowCameraParams {
            float distance = 5.0f;
            float yaw = 0.0f;
            float pitch = 20.0f;
            float smoothSpeed = 5.0f;
        };
        
        struct TransformParams {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float rx = 0.0f;
            float ry = 0.0f;
            float rz = 0.0f;
            float sx = 1.0f;
            float sy = 1.0f;
            float sz = 1.0f;
        };
        
        FollowCameraParams followCam;
        TransformParams transform;
        
        // Follow camera should have meaningful defaults
        CHECK(followCam.distance > 0.0f);
        CHECK(followCam.pitch >= -90.0f);
        CHECK(followCam.pitch <= 90.0f);
        
        // Transform for cameras should have scale = 1.0 (cameras don't scale)
        CHECK(transform.sx == doctest::Approx(1.0f));
        CHECK(transform.sy == doctest::Approx(1.0f));
        CHECK(transform.sz == doctest::Approx(1.0f));
    }
    
    TEST_CASE("MVC separation test") {
        // Model
        struct Model {
            float distance = 5.0f;
            float yaw = 0.0f;
            float pitch = 20.0f;
        };
        
        // View  
        struct View {
            bool showDistance = true;
            bool showYaw = true;
            bool showPitch = true;
            bool showTranslation = false;
            bool showRotation = false;
            bool showScale = false;
        };
        
        // Controller
        struct Controller {
            bool validateInput(float distance, float yaw, float pitch) {
                return distance > 0.0f && 
                       pitch >= -90.0f && pitch <= 90.0f;
            }
            
            Model processInput(float distance, float yaw, float pitch) {
                Model m;
                m.distance = distance;
                m.yaw = yaw;
                m.pitch = pitch;
                return m;
            }
            
            View createViewForModel(const Model& m) {
                View v;
                v.showDistance = true;
                v.showYaw = true;
                v.showPitch = true;
                v.showTranslation = false;  // Follow cameras don't have translation
                v.showRotation = false;     // Follow cameras don't have rotation
                v.showScale = false;        // Cameras don't have scale
                return v;
            }
        };
        
        Controller ctrl;
        
        // Test 1: Controller validates input (MVC separation)
        CHECK(ctrl.validateInput(5.0f, 0.0f, 30.0f) == true);
        CHECK(ctrl.validateInput(0.0f, 0.0f, 30.0f) == false); // Invalid distance
        CHECK(ctrl.validateInput(5.0f, 0.0f, 100.0f) == false); // Invalid pitch
        
        // Test 2: Model is created correctly
        Model m = ctrl.processInput(10.0f, 45.0f, 25.0f);
        CHECK(m.distance == doctest::Approx(10.0f));
        CHECK(m.yaw == doctest::Approx(45.0f));
        CHECK(m.pitch == doctest::Approx(25.0f));
        
        // Test 3: View shows correct controls for follow camera
        View v = ctrl.createViewForModel(m);
        CHECK(v.showDistance == true);
        CHECK(v.showYaw == true);
        CHECK(v.showPitch == true);
        CHECK(v.showTranslation == false);  // Key test: no translation for follow cam
        CHECK(v.showRotation == false);     // Key test: no rotation for follow cam
        CHECK(v.showScale == false);        // Cameras don't have scale
    }
}
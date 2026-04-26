#include "../../jolt/UnitTests/doctest.h"
#include "../../camera_follow_settings.h"
#include <cmath>

TEST_SUITE("Camera Follow Settings") {
    TEST_CASE("Default constructor initializes correctly") {
        FollowSettings settings;
        
        CHECK(settings.relativeYaw == doctest::Approx(0.0f));
        CHECK(settings.relativePitch == doctest::Approx(0.3f));
        CHECK(settings.distance == doctest::Approx(5.0f));
        CHECK(settings.smoothSpeed == doctest::Approx(followcam::kDefaultSmoothSpeed));
        CHECK(settings.targetOffset == glm::vec3(0.0f, 0.0f, 0.0f));
        CHECK(settings.enabled == false);
    }
    
    TEST_CASE("Sanitize settings clamps values correctly") {
        FollowSettings settings;
        settings.distance = 0.5f;  // Below minimum
        settings.relativePitch = 2.0f;  // Above maximum
        settings.smoothSpeed = -0.05f;  // Below minimum
        settings.relativeYaw = 10.0f;  // Will be normalized
        
        FollowSettings sanitized = followcam::sanitizeSettings(settings);
        
        CHECK(sanitized.distance == doctest::Approx(followcam::kMinDistance));
        CHECK(sanitized.relativePitch == doctest::Approx(followcam::kMaxPitchRadians));
        CHECK(sanitized.smoothSpeed == doctest::Approx(followcam::kMinSmoothSpeed));
        
        // Yaw should be normalized to [-π, π]
        CHECK(sanitized.relativeYaw >= -M_PI);
        CHECK(sanitized.relativeYaw <= M_PI);
    }
    
    TEST_CASE("Normalize angle function works correctly") {
        // Test normalization of angles outside [-π, π]
        float angle1 = followcam::normalizeAngleRadians(3.0f * M_PI);
        CHECK(std::abs(std::abs(angle1) - static_cast<float>(M_PI)) < 0.001f);
        
        float angle2 = followcam::normalizeAngleRadians(-3.0f * M_PI);
        CHECK(std::abs(std::abs(angle2) - static_cast<float>(M_PI)) < 0.001f);
        
        float angle3 = followcam::normalizeAngleRadians(0.5f * M_PI);
        CHECK(angle3 == doctest::Approx(0.5f * M_PI).epsilon(0.001f));
    }
}

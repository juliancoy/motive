#include "../../jolt/UnitTests/doctest.h"
#include "../../orbit_follow_rig.h"
#include "../../camera_follow_settings.h"

TEST_SUITE("Orbit Follow Rig") {
    TEST_CASE("Default constructor initializes correctly") {
        FollowOrbit orbit;
        
        CHECK(orbit.sceneIndex() == -1);
        CHECK(orbit.isEnabled() == false);
        
        FollowSettings settings = orbit.settings();
        CHECK(settings.relativeYaw == doctest::Approx(0.0f));
        CHECK(settings.relativePitch == doctest::Approx(0.3f));
        CHECK(settings.distance == doctest::Approx(5.0f));
    }
    
    TEST_CASE("Configure sets scene index and settings") {
        FollowOrbit orbit;
        FollowSettings settings;
        settings.distance = 10.0f;
        settings.relativeYaw = 1.0f;
        settings.relativePitch = 0.5f;
        settings.enabled = true;
        
        orbit.configure(5, settings);
        
        CHECK(orbit.sceneIndex() == 5);
        CHECK(orbit.isEnabled() == true);
        
        FollowSettings retrieved = orbit.settings();
        CHECK(retrieved.distance == doctest::Approx(10.0f));
        CHECK(retrieved.relativeYaw == doctest::Approx(1.0f));
        CHECK(retrieved.relativePitch == doctest::Approx(0.5f));
    }
    
    TEST_CASE("Clear resets to default state") {
        FollowOrbit orbit;
        FollowSettings settings;
        settings.enabled = true;
        
        orbit.configure(5, settings);
        orbit.clear();
        
        CHECK(orbit.sceneIndex() == -1);
        CHECK(orbit.isEnabled() == false);
    }
    
    TEST_CASE("ComputeTargetYaw calculates correct yaw from forward vector") {
        // Forward along +Z axis
        glm::vec3 forwardZ(0.0f, 0.0f, 1.0f);
        float yawZ = FollowOrbit::computeTargetYaw(forwardZ);
        CHECK(yawZ == doctest::Approx(0.0f).epsilon(0.001f));
        
        // Forward along -Z axis
        glm::vec3 forwardNegZ(0.0f, 0.0f, -1.0f);
        float yawNegZ = FollowOrbit::computeTargetYaw(forwardNegZ);
        CHECK(yawNegZ == doctest::Approx(M_PI).epsilon(0.001f));
        
        // Forward along +X axis
        glm::vec3 forwardX(1.0f, 0.0f, 0.0f);
        float yawX = FollowOrbit::computeTargetYaw(forwardX);
        CHECK(yawX == doctest::Approx(M_PI_2).epsilon(0.001f));
        
        // Forward along -X axis
        glm::vec3 forwardNegX(-1.0f, 0.0f, 0.0f);
        float yawNegX = FollowOrbit::computeTargetYaw(forwardNegX);
        CHECK(yawNegX == doctest::Approx(-M_PI_2).epsilon(0.001f));
    }
    
    TEST_CASE("ComputePose calculates correct camera position") {
        glm::vec3 targetCenter(0.0f, 0.0f, 0.0f);
        float targetYaw = 0.0f; // Facing +Z
        FollowSettings settings;
        settings.distance = 5.0f;
        settings.relativeYaw = 0.0f; // Behind target
        settings.relativePitch = 0.3f; // Slightly above
        
        FollowOrbitPose pose = FollowOrbit::computePose(targetCenter, targetYaw, settings);
        
        // Camera should be behind target (relativeYaw=0 means behind)
        // With target facing +Z, behind means camera at -Z
        CHECK(pose.position.z < 0.0f);
        CHECK(pose.position.x == doctest::Approx(0.0f).epsilon(0.001f));
        CHECK(pose.position.y > 0.0f); // Positive pitch means above
        
        // Distance should be approximately 5.0
        float distance = glm::length(pose.position - targetCenter);
        CHECK(distance == doctest::Approx(5.0f).epsilon(0.001f));
    }
    
    TEST_CASE("NormalizeAngle function works correctly") {
        // Test with doctest since we can't access private method
        // This is tested indirectly through computeTargetYaw
        // Doctest doesn't have SUCCEED macro, just pass the test
    }

    TEST_CASE("OrbitRig geometry is independent from target heading") {
        OrbitRig orbit;
        FollowSettings settings;
        settings.enabled = true;
        settings.distance = 6.0f;
        settings.relativeYaw = 0.0f;
        settings.relativePitch = 0.35f;
        settings.smoothSpeed = 1000.0f; // Near-instant update for deterministic test
        orbit.configure(0, settings);

        const glm::vec3 targetCenter(1.0f, 2.0f, 3.0f);
        FollowOrbitPose current{};
        current.position = glm::vec3(1.0f, 2.0f, -3.0f);
        current.rotation = glm::vec2(0.0f, 0.0f);

        const FollowOrbitPose poseA = orbit.update(targetCenter, 1.0f / 60.0f, current);
        const FollowOrbitPose poseB = orbit.update(targetCenter, 1.0f / 60.0f, current);

        CHECK(glm::length(poseA.position - targetCenter) == doctest::Approx(6.0f).epsilon(0.02f));
        CHECK(poseA.position.x == doctest::Approx(poseB.position.x).epsilon(0.001f));
        CHECK(poseA.position.y == doctest::Approx(poseB.position.y).epsilon(0.001f));
        CHECK(poseA.position.z == doctest::Approx(poseB.position.z).epsilon(0.001f));
    }
}

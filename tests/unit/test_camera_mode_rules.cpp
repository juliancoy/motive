#include "../../jolt/UnitTests/doctest.h"
#include "../../camera_mode_rules.h"

TEST_SUITE("Camera Mode Rules")
{
    TEST_CASE("Modes requiring follow target are identified correctly")
    {
        CHECK(camera_mode_rules::requiresFollowTarget(CameraMode::CharacterFollow));
        CHECK(camera_mode_rules::requiresFollowTarget(CameraMode::OrbitFollow));
        CHECK_FALSE(camera_mode_rules::requiresFollowTarget(CameraMode::FreeFly));
        CHECK_FALSE(camera_mode_rules::requiresFollowTarget(CameraMode::Fixed));
    }

    TEST_CASE("Follow modes require enabled follow target")
    {
        CHECK_FALSE(camera_mode_rules::canEnterMode(CameraMode::CharacterFollow, false, 2));
        CHECK_FALSE(camera_mode_rules::canEnterMode(CameraMode::CharacterFollow, true, -1));
        CHECK(camera_mode_rules::canEnterMode(CameraMode::CharacterFollow, true, 2));

        CHECK_FALSE(camera_mode_rules::canEnterMode(CameraMode::OrbitFollow, false, 2));
        CHECK_FALSE(camera_mode_rules::canEnterMode(CameraMode::OrbitFollow, true, -1));
        CHECK(camera_mode_rules::canEnterMode(CameraMode::OrbitFollow, true, 2));
    }

    TEST_CASE("Non-follow modes are always enterable")
    {
        CHECK(camera_mode_rules::canEnterMode(CameraMode::FreeFly, false, -1));
        CHECK(camera_mode_rules::canEnterMode(CameraMode::Fixed, false, -1));
    }
}


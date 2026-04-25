#include "../../jolt/UnitTests/doctest.h"
#include "../../input_mode_rules.h"

TEST_SUITE("Input Mode Rules")
{
    TEST_CASE("Character input is routed in follow modes with target")
    {
        CHECK(input_mode_rules::shouldRouteCharacterInput(CameraMode::CharacterFollow, true));
        CHECK_FALSE(input_mode_rules::shouldRouteCharacterInput(CameraMode::CharacterFollow, false));
        CHECK(input_mode_rules::shouldRouteCharacterInput(CameraMode::OrbitFollow, true));
        CHECK_FALSE(input_mode_rules::shouldRouteCharacterInput(CameraMode::OrbitFollow, false));
        CHECK_FALSE(input_mode_rules::shouldRouteCharacterInput(CameraMode::FreeFly, true));
        CHECK_FALSE(input_mode_rules::shouldRouteCharacterInput(CameraMode::Fixed, true));
    }

    TEST_CASE("Camera movement is only allowed in FreeFly with controls enabled")
    {
        CHECK(input_mode_rules::shouldMoveCamera(CameraMode::FreeFly, true));
        CHECK_FALSE(input_mode_rules::shouldMoveCamera(CameraMode::FreeFly, false));
        CHECK_FALSE(input_mode_rules::shouldMoveCamera(CameraMode::CharacterFollow, true));
        CHECK_FALSE(input_mode_rules::shouldMoveCamera(CameraMode::OrbitFollow, true));
        CHECK_FALSE(input_mode_rules::shouldMoveCamera(CameraMode::Fixed, true));
    }
}

#include "../../jolt/UnitTests/doctest.h"
#include "../../follow_target_tracker.h"

#include <cmath>
#include <vector>

TEST_SUITE("Follow Target Tracker")
{
    TEST_CASE("First update initializes motion center at raw center")
    {
        FollowTargetTracker tracker;
        const FollowTargetFrame frame = tracker.update(glm::vec3(1.0f, 2.0f, 3.0f), 1.0f / 60.0f, 5.0f);
        CHECK(frame.valid == true);
        CHECK(frame.rawCenter.x == doctest::Approx(1.0f));
        CHECK(frame.rawCenter.y == doctest::Approx(2.0f));
        CHECK(frame.rawCenter.z == doctest::Approx(3.0f));
        CHECK(frame.motionCenter.x == doctest::Approx(1.0f));
        CHECK(frame.motionCenter.y == doctest::Approx(2.0f));
        CHECK(frame.motionCenter.z == doctest::Approx(3.0f));
    }

    TEST_CASE("Tracker smooths alternating jitter")
    {
        FollowTargetTracker tracker;
        const float dt = 1.0f / 60.0f;
        const float smoothSpeed = 5.0f;

        tracker.update(glm::vec3(0.0f, 0.0f, 0.0f), dt, smoothSpeed);

        std::vector<float> raw;
        std::vector<float> smoothed;
        raw.reserve(120);
        smoothed.reserve(120);

        for (int i = 0; i < 120; ++i)
        {
            const float rawX = (i % 2 == 0) ? 0.25f : -0.25f;
            const FollowTargetFrame frame = tracker.update(glm::vec3(rawX, 0.0f, 0.0f), dt, smoothSpeed);
            raw.push_back(rawX);
            smoothed.push_back(frame.motionCenter.x);
        }

        auto stddev = [](const std::vector<float>& values) -> float {
            float mean = 0.0f;
            for (float v : values) mean += v;
            mean /= static_cast<float>(values.size());
            float variance = 0.0f;
            for (float v : values)
            {
                const float d = v - mean;
                variance += d * d;
            }
            variance /= static_cast<float>(values.size());
            return std::sqrt(variance);
        };

        const float rawStd = stddev(raw);
        const float smoothStd = stddev(smoothed);
        CHECK(smoothStd < rawStd * 0.65f);
    }

    TEST_CASE("Tracker converges toward new target")
    {
        FollowTargetTracker tracker;
        const float dt = 1.0f / 60.0f;
        const float smoothSpeed = 6.0f;

        tracker.update(glm::vec3(0.0f, 0.0f, 0.0f), dt, smoothSpeed);

        FollowTargetFrame frame{};
        for (int i = 0; i < 120; ++i)
        {
            frame = tracker.update(glm::vec3(2.0f, 0.0f, 0.0f), dt, smoothSpeed);
        }

        CHECK(frame.motionCenter.x == doctest::Approx(2.0f).epsilon(0.01f));
        CHECK(frame.motionCenter.y == doctest::Approx(0.0f).epsilon(0.01f));
        CHECK(frame.motionCenter.z == doctest::Approx(0.0f).epsilon(0.01f));
    }

    TEST_CASE("Reset clears smoothing state")
    {
        FollowTargetTracker tracker;
        const float dt = 1.0f / 60.0f;

        tracker.update(glm::vec3(5.0f, 0.0f, 0.0f), dt, 5.0f);
        tracker.update(glm::vec3(0.0f, 0.0f, 0.0f), dt, 5.0f);
        tracker.reset();

        const FollowTargetFrame frame = tracker.update(glm::vec3(3.0f, 0.0f, 0.0f), dt, 5.0f);
        CHECK(frame.motionCenter.x == doctest::Approx(3.0f));
    }
}


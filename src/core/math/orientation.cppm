module;
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
export module GPP.Core:Orientation;

import std;

export namespace GPP
{
    // Identity orientation looks down -Z with +Y up.
    [[nodiscard]] inline glm::quat LookAtRotation(const glm::vec3& eye, const glm::vec3& target,
                                                  const glm::vec3& up = {0.0f, 1.0f, 0.0f})
    {
        const glm::vec3 delta = target - eye;
        if (glm::dot(delta, delta) < 1.0e-12f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        const glm::vec3 forward = glm::normalize(delta);
        const glm::vec3 safeUp = std::abs(glm::dot(forward, glm::normalize(up))) > 0.9999f
                                     ? glm::vec3(0.0f, 0.0f, 1.0f)
                                     : up;
        return glm::quatLookAt(forward, safeUp);
    }

    [[nodiscard]] inline glm::vec3 LookAtEulerDegrees(const glm::vec3& eye, const glm::vec3& target)
    {
        return glm::degrees(glm::eulerAngles(LookAtRotation(eye, target)));
    }
}

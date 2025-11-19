#include "light.h"
#include <glm/gtc/constants.hpp>

namespace
{
glm::vec3 normalizeOrDefault(const glm::vec3& vec, const glm::vec3& fallback)
{
    float lengthSq = glm::dot(vec, vec);
    if (lengthSq <= 0.0f)
    {
        return fallback;
    }
    return glm::normalize(vec);
}
} // namespace

Light::Light()
    : direction(glm::vec3(0.0f, 0.0f, 1.0f)),
      ambient(glm::vec3(0.1f)),
      diffuse(glm::vec3(0.9f))
{
}

Light::Light(const glm::vec3& dir,
             const glm::vec3& ambientColor,
             const glm::vec3& diffuseColor)
    : direction(normalizeOrDefault(dir, glm::vec3(0.0f, 0.0f, 1.0f))),
      ambient(ambientColor),
      diffuse(diffuseColor)
{
}

void Light::setDirection(const glm::vec3& dir)
{
    direction = normalizeOrDefault(dir, direction);
}

void Light::setAmbient(const glm::vec3& ambientColor)
{
    ambient = ambientColor;
}

void Light::setDiffuse(const glm::vec3& diffuseColor)
{
    diffuse = diffuseColor;
}

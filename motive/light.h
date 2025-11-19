#pragma once

#include <glm/glm.hpp>

struct Light
{
    Light();
    Light(const glm::vec3& direction,
          const glm::vec3& ambient,
          const glm::vec3& diffuse);

    void setDirection(const glm::vec3& dir);
    void setAmbient(const glm::vec3& ambientColor);
    void setDiffuse(const glm::vec3& diffuseColor);

    glm::vec3 direction;
    glm::vec3 ambient;
    glm::vec3 diffuse;
};

struct LightUBOData
{
    glm::vec4 direction;
    glm::vec4 ambient;
    glm::vec4 diffuse;
};

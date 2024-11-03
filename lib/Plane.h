#pragma once

#include <glm/glm.hpp>

struct Plane
{
    Plane();
    ~Plane() = default;

    glm::vec3 position;
    glm::vec3 normal;

    bool pointInFront(glm::vec3 point) const;
};
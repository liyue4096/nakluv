#include "Plane.h"

Plane::Plane()
{
    position = glm::vec3(0.0f, 0.0f, 0.0f);
    normal = glm::vec3(0.0f, 1.0f, 0.0f);
}

bool Plane::pointInFront(glm::vec3 point) const
{
    return glm::dot(point - position, normal) > 0;
}
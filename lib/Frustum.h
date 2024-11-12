#pragma once

#include <glm/glm.hpp>
#include "Plane.h"
#include "Bbox.h"
#include "Camera_new.h"

/* cr. structure reference from Learn OpenGL: https://learnopengl.com/Guest-Articles/2021/Scene/Frustum-Culling */
struct Frustum
{
    Plane topFace;
    Plane bottomFace;
    Plane leftFace;
    Plane rightFace;
    Plane nearFace;
    Plane farFace;

    Frustum() = default;
    ~Frustum() = default;

    static Frustum createFrustumFromCamera(const Camera_new &camera);
    static Frustum createFrustumFromMatrix(const glm::mat4 &cilp_from_world);
    bool isBBoxInFrustum(BBox &bbox);
};
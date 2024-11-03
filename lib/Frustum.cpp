#include "Frustum.h"

// Helper function to calculate a point on the plane
glm::vec3 calculatePointOnPlane(const glm::mat4 &m, const glm::vec3 &normal)
{
    // Calculate the camera position from the inverse matrix
    glm::vec3 cameraPosition = glm::vec3(glm::inverse(m)[3]);

    // Use the camera position as a reference point on each plane
    return cameraPosition + normal;
}

Frustum Frustum::createFrustumFromMatrix(const glm::mat4 &clip_from_world)
{
    Frustum frustum;

    // Left Plane
    frustum.leftFace.normal = glm::normalize(glm::vec3(clip_from_world[0][3] + clip_from_world[0][0],
                                                       clip_from_world[1][3] + clip_from_world[1][0],
                                                       clip_from_world[2][3] + clip_from_world[2][0]));
    frustum.leftFace.position = calculatePointOnPlane(clip_from_world, frustum.leftFace.normal);

    // Right Plane
    frustum.rightFace.normal = glm::normalize(glm::vec3(clip_from_world[0][3] - clip_from_world[0][0],
                                                        clip_from_world[1][3] - clip_from_world[1][0],
                                                        clip_from_world[2][3] - clip_from_world[2][0]));
    frustum.rightFace.position = calculatePointOnPlane(clip_from_world, frustum.rightFace.normal);

    // Bottom Plane
    frustum.bottomFace.normal = glm::normalize(glm::vec3(clip_from_world[0][3] + clip_from_world[0][1],
                                                         clip_from_world[1][3] + clip_from_world[1][1],
                                                         clip_from_world[2][3] + clip_from_world[2][1]));
    frustum.bottomFace.position = calculatePointOnPlane(clip_from_world, frustum.bottomFace.normal);

    // Top Plane
    frustum.topFace.normal = glm::normalize(glm::vec3(clip_from_world[0][3] - clip_from_world[0][1],
                                                      clip_from_world[1][3] - clip_from_world[1][1],
                                                      clip_from_world[2][3] - clip_from_world[2][1]));
    frustum.topFace.position = calculatePointOnPlane(clip_from_world, frustum.topFace.normal);

    // Near Plane
    frustum.nearFace.normal = glm::normalize(glm::vec3(clip_from_world[0][3] + clip_from_world[0][2],
                                                       clip_from_world[1][3] + clip_from_world[1][2],
                                                       clip_from_world[2][3] + clip_from_world[2][2]));
    frustum.nearFace.position = calculatePointOnPlane(clip_from_world, frustum.nearFace.normal);

    // Far Plane
    frustum.farFace.normal = glm::normalize(glm::vec3(clip_from_world[0][3] - clip_from_world[0][2],
                                                      clip_from_world[1][3] - clip_from_world[1][2],
                                                      clip_from_world[2][3] - clip_from_world[2][2]));
    frustum.farFace.position = calculatePointOnPlane(clip_from_world, frustum.farFace.normal);

    return frustum;
}

bool Frustum::isBBoxInFrustum(BBox &bbox)
{
    /* cr. Frustum Culling by Dion Picco: https://www.flipcode.com/archives/Frustum_Culling.shtml */

    std::vector<glm::vec3> bboxCorners = bbox.corners();
    int sizeCorners = (int)bboxCorners.size();

    int cornerInFrustumCnt = 0;

    for (int i = 0; i < sizeCorners; ++i)
    {
        int cornerInPlanerFrontCnt = 0;

        for (const Plane &plane : {nearFace, farFace, leftFace, rightFace, topFace, bottomFace})
        {
            if (plane.pointInFront(bboxCorners[i]))
                ++cornerInPlanerFrontCnt;
        }

        if (cornerInPlanerFrontCnt == 6) // pass if corner is in front of all planes
            ++cornerInFrustumCnt;
    }

    return (cornerInFrustumCnt >= 4); // pass if >= 6 corners are in the frustum
}
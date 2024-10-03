// this code base on 15462 A3, I use it in spring 2024

#pragma once

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <ostream>
#include <vector>

#include "mat4.hpp"
// #include "ray.h"
// #include "vec2.h"
// #include "vec3.h"

struct Plane
{
    glm::vec3 normal;
    float distance;
};

/// Take minimum of each component
inline glm::vec3 hmin(glm::vec3 l, glm::vec3 r)
{
    return glm::vec3(std::min(l.x, r.x), std::min(l.y, r.y), std::min(l.z, r.z));
}

/// Take maximum of each component
inline glm::vec3 hmax(glm::vec3 l, glm::vec3 r)
{
    return glm::vec3(std::max(l.x, r.x), std::max(l.y, r.y), std::max(l.z, r.z));
}

struct BBox
{

    /// Default min is max float value, default max is negative max float value
    BBox() : min(FLT_MAX), max(-FLT_MAX)
    {
    }
    /// Set minimum and maximum extent
    explicit BBox(glm::vec3 min, glm::vec3 max) : min(min), max(max)
    {
    }

    BBox(const BBox &) = default;
    BBox &operator=(const BBox &) = default;
    ~BBox() = default;

    /// Rest min to max float, max to negative max float
    void reset()
    {
        min = glm::vec3(FLT_MAX);
        max = glm::vec3(-FLT_MAX);
    }

    /// Expand bounding box to include point
    void enclose(glm::vec3 point)
    {
        min = hmin(min, point);
        max = hmax(max, point);
    }
    void enclose(BBox box)
    {
        min = hmin(min, box.min);
        max = hmax(max, box.max);
    }

    /// Get center point of box
    glm::vec3 center() const
    {
        return (min + max) * 0.5f;
    }

    // Check whether box has no volume
    bool empty() const
    {
        return min.x > max.x || min.y > max.y || min.z > max.z;
    }

    // Get surface area of the box
    float surface_area() const
    {
        if (empty())
            return 0.0f;
        glm::vec3 extent = max - min;
        return 2.0f * (extent.x * extent.z + extent.x * extent.y + extent.y * extent.z);
    }

    // Transform box by a matrix
    BBox transform(const glm::mat4 &trans)
    {
        glm::vec3 amin = min, amax = max;
        min = max = glm::vec3(trans[3]);
        for (uint32_t i = 0; i < 3; i++)
        {
            for (uint32_t j = 0; j < 3; j++)
            {
                float a = trans[j][i] * amin[j];
                float b = trans[j][i] * amax[j];
                if (a < b)
                {
                    min[i] += a;
                    max[i] += b;
                }
                else
                {
                    min[i] += b;
                    max[i] += a;
                }
            }
        }
        return *this;
    }

    // bool hit(const Ray &ray, Vec2 &times) const

    // Get the eight corner points of the bounding box
    std::vector<glm::vec3> corners() const
    {
        std::vector<glm::vec3> ret(8);
        ret[0] = glm::vec3(min.x, min.y, min.z);
        ret[1] = glm::vec3(max.x, min.y, min.z);
        ret[2] = glm::vec3(min.x, max.y, min.z);
        ret[3] = glm::vec3(min.x, min.y, max.z);
        ret[4] = glm::vec3(max.x, max.y, min.z);
        ret[5] = glm::vec3(min.x, max.y, max.z);
        ret[6] = glm::vec3(max.x, min.y, max.z);
        ret[7] = glm::vec3(max.x, max.y, max.z);
        return ret;
    }

    /// Given a screen transformation (projection), calculate screen-space ([-1,1]x[-1,1])
    /// bounds that will always contain the bounding box on screen
    // void screen_rect(const Mat4 &transform, Vec2 &min_out, Vec2 &max_out) const
    // {

    //     min_out = glm::vec2(FLT_MAX);
    //     max_out = glm::vec2(-FLT_MAX);
    //     auto c = corners();
    //     bool partially_behind = false, all_behind = true;
    //     for (auto &v : c)
    //     {
    //         glm::vec3 p = transform * v;
    //         if (p.z < 0)
    //         {
    //             partially_behind = true;
    //         }
    //         else
    //         {
    //             all_behind = false;
    //         }
    //         min_out = hmin(min_out, glm::vec2(p.x, p.y));
    //         max_out = hmax(max_out, glm::vec2(p.x, p.y));
    //     }

    //     if (partially_behind && !all_behind)
    //     {
    //         min_out = glm::vec2(-1.0f, -1.0f);
    //         max_out = glm::vec2(1.0f, 1.0f);
    //     }
    //     else if (all_behind)
    //     {
    //         min_out = glm::vec2(0.0f, 0.0f);
    //         max_out = glm::vec2(0.0f, 0.0f);
    //     }
    // }

    glm::vec3 min, max;

    bool is_bbox_outside_frustum(const std::array<Plane, 6> &planes)
    {
        for (const Plane &plane : planes)
        {
            glm::vec3 nearest_point = min;
            if (plane.normal.x > 0)
                nearest_point.x = max.x;
            if (plane.normal.y > 0)
                nearest_point.y = max.y;
            if (plane.normal.z > 0)
                nearest_point.z = max.z;

            if (glm::dot(plane.normal, nearest_point) + plane.distance < 0)
            {
                return true;
            }
        }
        return false;
    }
};

// inline std::ostream &operator<<(std::ostream &out, BBox b)
// {
//     out << "BBox{" << b.min << "," << b.max << "}";
//     return out;
// }

inline std::array<Plane, 6> extract_planes(const glm::mat4 &matrix)
{
    std::array<Plane, 6> planes;

    // Left
    planes[0].normal = glm::vec3(matrix[0][3] + matrix[0][0], matrix[1][3] + matrix[1][0], matrix[2][3] + matrix[2][0]);
    planes[0].distance = matrix[3][3] + matrix[3][0];

    // Right
    planes[1].normal = glm::vec3(matrix[0][3] - matrix[0][0], matrix[1][3] - matrix[1][0], matrix[2][3] - matrix[2][0]);
    planes[1].distance = matrix[3][3] - matrix[3][0];

    // Bottom
    planes[2].normal = glm::vec3(matrix[0][3] + matrix[0][1], matrix[1][3] + matrix[1][1], matrix[2][3] + matrix[2][1]);
    planes[2].distance = matrix[3][3] + matrix[3][1];

    // Top
    planes[3].normal = glm::vec3(matrix[0][3] - matrix[0][1], matrix[1][3] - matrix[1][1], matrix[2][3] - matrix[2][1]);
    planes[3].distance = matrix[3][3] - matrix[3][1];

    // Near
    // planes[4].normal = glm::vec3(matrix[0][3] + matrix[0][2], matrix[1][3] + matrix[1][2], matrix[2][3] + matrix[2][2]);
    // planes[4].distance = matrix[3][3] + matrix[3][2];
    planes[4].normal = glm::vec3(matrix[0][2], matrix[1][2], matrix[2][2]);
    planes[4].distance = matrix[3][2];

    // Far
    planes[5].normal = glm::vec3(matrix[0][3] - matrix[0][2], matrix[1][3] - matrix[1][2], matrix[2][3] - matrix[2][2]);
    planes[5].distance = matrix[3][3] - matrix[3][2];

    // Normalize the planes
    for (auto &plane : planes)
    {
        float length = glm::length(plane.normal);
        plane.normal /= length;
        plane.distance /= length;
    }

    return planes;
}
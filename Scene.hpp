#pragma once

#include "include/glm/glm.hpp"
#include "include/sejp/sejp.hpp"
#include <vulkan/vulkan_core.h>

#include <cstring>
#include <iostream>
#include <memory>
#include <vector>
#include <variant>
#include <unordered_map>
#include <map>
#include <optional>

struct Mesh;
struct Camera;

struct Scene
{
    std::string name;
    std::vector<std::variant<std::string, double>> roots; // roots can be either string or number
};

struct Node
{
    std::string name;

    struct
    {
        float tx = 0.f, ty = 0.f, tz = 0.f;
    } Translation;

    struct
    {
        float rx = 0.f, ry = 0.f, rz = 0.f, rw = 1.f;
    } Rotation;

    struct
    {
        float sx = 1.f, sy = 1.f, sz = 1.f;
    } Scale;

    std::vector<std::variant<std::string, double>> children;

    std::string mesh_name;
    std::string camera_name;
    std::string environment_name;
    std::string light_name;

    struct Mesh *mesh_;
    struct Camera *camera_;
};

struct Mesh
{
    std::string name;
    std::string topology;
    uint32_t count;

    struct
    {
        std::string src;
        uint32_t offset;
        std::string format;
    } Indices; // (optional) -- if specified, a data stream containing indices for indexed drawing commands.

    // Attributes structure to hold multiple named attributes
    struct Attribute
    {
        std::string src;
        uint32_t offset;
        uint32_t stride;
        std::string format;
    };

    std::map<std::string, Attribute> attributes; // Map to hold named attributes (POSITION, NORMAL, etc.)

    std::string material;
};

struct Camera
{
    std::string name;
    struct
    {
        float aspect = 0.f, vfov = 0.f, near = 0.f, far = 0.f;
    } perspective; //(optional)
};

struct S72_scene
{
    struct Scene scene;
    // std::vector<Node *> roots;
    std::unordered_map<std::string, Node *> roots;
    std::vector<Node> nodes;
    std::vector<Mesh> meshes;
    std::vector<Camera> cameras;
};

void get_scene(const std::vector<sejp::value> &array);
Mesh *find_mesh_by_name(const std::string &mesh_name);
Camera *find_camera_by_name(const std::string &camera_name);
Node *find_node_by_name_or_index(const std::variant<std::string, double> &root);
void dfs_build_tree(Node *current_node);
void build_node_trees();
void scene_workflow(sejp::value &val);
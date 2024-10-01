#pragma once

#include "include/sejp/sejp.hpp"
#include <vulkan/vulkan_core.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstring>
#include <iostream>
#include <memory>
#include <vector>
#include <variant>
#include <unordered_map>
#include <map>
#include <optional>

#include "lib/bbox.h"

struct Mesh;
struct Camera;
struct Driver;

enum Animation_Mode
{
    PAUSE,
    PLAY,
};

enum Camera_Mode
{
    SCENE = 1,
    USER = 2,
    DEBUG = 3,
};

enum Cull_Mode
{
    DEFAULT,
    NONE,
    FRUSTUM,
};

enum DriverChannleType
{
    TRANSLATION,
    SCALE,
    ROTATION,
};

enum DriverInterpolation
{
    STEP,
    LINEAR,
    SLERP,
};

enum ProjectionType
{
    Perspective,
    Orthographic
};

enum MaterialType
{
    PBR,
    LAMBERTIAN,
    MIRROR,
    ENVIRONMENT,
};

struct Scene
{
    std::string name;
    std::vector<std::variant<std::string, double>> roots; // roots can be either string or number
};

struct Node
{
    std::string name;

    // The core function of a transform is to store a transformation in the world:
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // n.b. wxyz init order
    glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f);

    std::vector<std::variant<std::string, double>> children;
    std::vector<Node *> children_node_;

    std::string mesh_name;
    std::string camera_name;
    std::string environment_name;
    std::string light_name;

    Node *parent_ = nullptr;

    Mesh *mesh_ = nullptr;
    Camera *camera_ = nullptr;
    // Driver *driver_ = nullptr;

    // void make_animation(float time);

    // ..relative to its parent:
    glm::mat4x3 make_local_to_parent() const;
    glm::mat4x3 make_parent_to_local() const;
    // ..relative to the world:
    glm::mat4x3 make_local_to_world() const;
    glm::mat4x3 make_world_to_local() const;

    void child_forward_kinematics_transforms(Node *node_);
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
    glm::mat4 make_projection() const;
};

struct Driver
{
    std::string name;
    std::string refnode_name; // target object
    DriverChannleType channel;
    uint32_t channel_dim;

    struct Frame
    {
        float time;
        std::vector<float> value;
    };

    std::vector<Frame> frames;

    DriverInterpolation interpolation = DriverInterpolation::LINEAR;

    // for animation use
    uint32_t current_frame = 0;
    uint32_t next_frame = 0;

    glm::vec3 position_init = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 scale_init = glm::vec3(1.0f, 1.0f, 1.0f);
    glm::quat rotation_init = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // n.b. wxyz init order

    void make_animation(float time);
};

// struct MaterialObject
// {
//     std::string name;
//     std::optional<Texture> normalmap;       // std::nullopt
//     std::optional<Texture> displacementmap; // std::nullopt
//     MaterialType type;
//     std::variant<std::monostate, PBRMaterial, LambertianMaterial> material; // std::monostate
// };

// struct EnvironmentObject
// {
//     std::string name;
//     Texture radiance;
// };

// struct LightObject
// {
//     std::string name;
//     glm::vec3 tint;
//     std::variant<SunLight, SphereLight, SpotLight> light;
//     uint32_t shadow;
// };

struct S72_scene
{
    struct Scene scene;
    float animation_duration = 0.f;
    // std::vector<Node *> roots;
    std::unordered_map<std::string, Node *> nodes_map;
    std::unordered_map<std::string, std::vector<Node *>> cameras_path;
    std::unordered_map<Node *, glm::mat4> transforms;
    std::unordered_map<Node *, BBox> bboxes;
    std::vector<Node> nodes;
    std::vector<Mesh> meshes;
    std::vector<Camera> cameras;
    std::vector<Driver> drivers;
    Camera_Mode camera_mode = SCENE;
    Camera *current_camera_;
};

void get_scene(const std::vector<sejp::value> &array);
Mesh *find_mesh_by_name(const std::string &mesh_name);
Camera *find_camera_by_name(const std::string &camera_name);
Node *find_node_by_name_or_index(const std::variant<std::string, double> &root);
void dfs_build_tree(Node *current_node, Node *parrent_node, std::vector<Node *> &);
void build_node_trees();
void bind_driver();

// set up all the info from s72 file
void scene_workflow(sejp::value &val);

glm::mat4 generate_transform(const Node *node);
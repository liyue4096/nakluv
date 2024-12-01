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
#include <tuple>
#include <optional>

#include "lib/Bbox.h"
#include "lib/Camera_new.h"

struct Mesh;
struct Camera;
struct Driver;
struct LightObject;

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

enum LightType
{
    SUN,
    SPHERE,
    SPOT,
};

struct Texture
{
    std::string src;
    std::string type;
    std::string format;
};

struct MeshVertices
{
    uint32_t first = 0;
    uint32_t count = 0;
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
    LightObject *light_ = nullptr;
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

using AlbedoParam = std::variant<glm::vec3, Texture>;
using RoughnessParam = std::variant<float, Texture>;
using MetalnessParam = std::variant<float, Texture>;

struct PBRMaterial
{
    AlbedoParam albedo;
    RoughnessParam roughness;
    MetalnessParam metalness;

    PBRMaterial() : albedo(glm::vec3(1.0f)),
                    roughness(0.5f),
                    metalness(0.5f) {}

    PBRMaterial(glm::vec3 albedo, float roughness, float metalness) : albedo(albedo),
                                                                      roughness(roughness),
                                                                      metalness(metalness) {}
};

struct LambertianMaterial
{
    AlbedoParam albedo;

    LambertianMaterial() : albedo(glm::vec3(0.8f, 0.8f, 0.8f)) {}

    LambertianMaterial(glm::vec3 albedo) : albedo(albedo) {}
};

struct MaterialObject
{
    std::string name;
    std::optional<Texture> normalmap;       // std::nullopt
    std::optional<Texture> displacementmap; // std::nullopt
    MaterialType type;
    std::variant<std::monostate, PBRMaterial, LambertianMaterial> material; // std::monostate
};

struct Environment
{
    std::string name = "";
    Texture radiance;
};

struct SunLight
{
    float angle;
    float strength;
};

struct SphereLight
{
    float radius;
    float power;
    float limit;
    float padding;
};

struct SpotLight
{
    float radius;
    float power;
    float fov;
    float blend;
    float limit;
    float padding;
};

struct LightObject
{
    std::string name;
    glm::vec3 tint = {1.f, 1.f, 1.f};
    float padding = 0;
    LightType type;
    uint32_t shadow = 0;

    union
    {
        float data_lookup[6];
        SunLight sun;
        SphereLight sphere;
        SpotLight spot;
    } data;
};

struct PTerrainObject
{
    std::string name;
    int length;
    int depth;
    int block_size;
    int octaves = 6;
    float persistence = 0.5f;
    float scale = 5.f;
    float height_limit = 10.f;
    std::string control_image;
    std::string noise_source;
    std::string material_name;
    MaterialObject *material_;
};
// Define block coordinates as a 3D integer tuple
using BlockCoord = std::tuple<int, int, int>;
#ifndef POOL_HPP
#define POOL_HPP

inline constexpr int POOL_SIZE = 256;

#endif

struct BlockCoordHash
{
    std::size_t operator()(const std::tuple<int, int, int> &coord) const
    {
        auto [x, y, z] = coord;

        // Mix the coordinates using prime multipliers
        std::size_t hashValue = (std::hash<int>()(x) * 73856093) ^
                                (std::hash<int>()(y) * 19349663) ^
                                (std::hash<int>()(z) * 83492791);

        // Map to the range [0, 127]
        return hashValue % POOL_SIZE;
    }
};

struct S72_scene
{
    struct Scene scene;
    float animation_duration = 0.f;
    std::unordered_map<std::string, Node *> nodes_map;
    std::unordered_map<std::string, std::vector<Node *>> cameras_path;
    std::unordered_map<Node *, glm::mat4> transforms;
    std::unordered_map<Mesh *, MeshVertices> mesh_vertices_map;
    std::unordered_map<Mesh *, BBox> mesh_bbox_map;
    std::unordered_map<Mesh *, MaterialObject *> mesh_material_map;
    std::unordered_map<MaterialObject *, std::vector<int>> material_textureindex_map; // index 0: albedo, 1: normal map, 2: displacement map
    std::unordered_map<MaterialObject *, int> material_descriptor_index_map;
    std::unordered_map<LightObject *, std::vector<Node *>> light_node_map;
    std::unordered_map<std::string, Camera_new *> cameraObject_map;
    std::vector<Node> nodes;
    std::vector<Mesh> meshes;
    std::vector<MaterialObject> materials;
    std::vector<Camera> cameras;
    std::vector<Camera_new> cameras_new;
    std::vector<Driver> drivers;
    std::vector<LightObject> lights;
    std::vector<std::string> textures_src;
    std::unordered_map<std::string, uint32_t> textures_src_index_map;
    Camera_Mode camera_mode = SCENE;
    Camera *current_camera_;
    Camera_new *current_camera_new_;
    Environment environment; // unique
    PTerrainObject terrain;
    // Map block coordinates to texture indices in the pool
    std::unordered_map<BlockCoord, int, BlockCoordHash> block_terrain_map;
};

void get_scene(const std::vector<sejp::value> &array);
Mesh *find_mesh_by_name(const std::string &mesh_name);
Camera *find_camera_by_name(const std::string &camera_name);
LightObject *find_light_by_name(const std::string &light_name);

Node *find_node_by_name_or_index(const std::variant<std::string, double> &root);
Node *find_node_by_name(std::string &str);
void dfs_build_tree(Node *current_node, Node *parrent_node, std::vector<Node *> &);
void build_node_trees();
void bind_driver();
void make_user_camera();

uint32_t convertToE5B9G9R9(float r, float g, float b);

glm::quat extract_rotation_quaternion(glm::mat4 &localToWorld);

void setup_material_textureindex_map();

// set up all the info from s72 file
void scene_workflow(sejp::value &val);

// print up all s72 msg
void print_s72();

glm::mat4 generate_transform(const Node *node);
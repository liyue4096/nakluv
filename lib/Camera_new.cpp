#include "Camera_new.h"
#include "../Scene.hpp"

extern S72_scene s72_scene;

Camera_new::Camera_new()
{
    // camera modes related
    camera_attributes.aspect = 1.5f;
    camera_attributes.vfov = glm::radians(60.0f);
    camera_attributes.near = 0.1f;
    camera_attributes.far = 1000.0f;

    camera_mode_cnt = 2;
    current_camera_mode = USER;

    // camera status
    movements.up = false;
    movements.down = false;
    movements.left = false;
    movements.right = false;
    movements.forward = false;
    movements.backward = false;

    postures.yaw_left = false;
    postures.yaw_right = false;
    postures.pitch_up = false;
    postures.pitch_down = false;

    // camera settings
    sensitivity.kb_forward = 0.15f;
    sensitivity.kb_rightward = 0.1f;
    sensitivity.kb_upward = 0.08f;
    sensitivity.kb_yaw = 0.5f;
    sensitivity.kb_pitch = 0.25f;
    sensitivity.mouse_yaw = 0.1f;
    sensitivity.mouse_pitch = 0.1f;

    unit_angle = 1.f;
    unit_sensitivity = 0.001f;

    position = glm::vec3{0.0f, -5.0f, 0.0f}; // s72 coord
    target_position = glm::vec3{0.0f, 0.0f, 0.0f};

    up = glm::vec3{0.0f, 0.0f, 1.0f};
    right = glm::vec3{1.0f, 0.f, 0.f};
    front = glm::vec3{0.0f, 1.0f, 0.0f};

    roll = 0.f;
    update_camera_eular_angles_from_vectors();
}

Camera_new::~Camera_new()
{
}

void Camera_new::reset_camera_control_status()
{
    movements.up = false;
    movements.down = false;
    movements.left = false;
    movements.right = false;
    movements.forward = false;
    movements.backward = false;

    postures.yaw_left = false;
    postures.yaw_right = false;
    postures.pitch_up = false;
    postures.pitch_down = false;
}

void Camera_new::update_camera_eular_angles_from_vectors()
{
    yaw = glm::degrees(atan2(front.x, front.y));                                       // looking forward along +y, rotating around +x
    pitch = glm::degrees(atan2(front.z, sqrt(front.x * front.x + front.y * front.y))); // looking forward along +z, rotating around +x

    // update_camera_vectors_from_eular_angles();
}

void Camera_new::update_camera_vectors_from_eular_angles()
{
    /* cr. https://learnopengl.com/Getting-started/Camera based on OpenGL coordinates (+Y up, -Z forward, +X right),
           correct the formulas based on Vulkan coordinates (-Y up, +Z forward, +X right)  */

    if (pitch > 89.f)
        pitch = 89.f;
    if (pitch < -89.f)
        pitch = -89.f;

    if (yaw > 180.f)
        yaw -= 360.f;
    if (yaw < -180.f)
        yaw += 360.f;

    const float yawRad = glm::radians(yaw);
    const float pitchRad = glm::radians(pitch);

    const float sy = sin(yawRad);
    const float cy = cos(yawRad);
    const float sp = sin(pitchRad);
    const float cp = cos(pitchRad);

    front.x = sy * cp; // +X right
    front.y = cy * cp; // +Y forward
    front.z = sp;      // +Z forward

    front = glm::normalize(front);

    // up remains world_up {0.0f, 0.0f, 1.0f}
    right = glm::normalize(glm::cross(front, up));
}

void Camera_new::update_info_from_another_camera(const Camera_new &updateFrom)
{
    camera_attributes.vfov = updateFrom.camera_attributes.vfov;
    camera_attributes.aspect = updateFrom.camera_attributes.aspect;
    camera_attributes.near = updateFrom.camera_attributes.near;
    camera_attributes.far = updateFrom.camera_attributes.far;

    position = updateFrom.position;
    front = updateFrom.front;
    up = updateFrom.up;
    yaw = updateFrom.yaw;
    pitch = updateFrom.pitch;
    roll = updateFrom.roll;

    reset_camera_control_status();
}

void Camera_new::update_camera_from_local_to_world(glm::mat4 &localToWorld)
{
    front = -glm::normalize(glm::vec3(localToWorld[2][0], localToWorld[2][1], localToWorld[2][2])); // make camera look toward -Z
    // up remains world_up {0.0f, 0.0f, 1.0f}
    right = glm::normalize(glm::cross(front, up));
    position = glm::vec3(localToWorld[3][0], localToWorld[3][1], localToWorld[3][2]);

    update_camera_eular_angles_from_vectors();

    reset_camera_control_status();
}

glm::mat4 Camera_new::calculate_camera_clip_from_world(Camera_new &camera)
{
    glm::vec3 target_direction = camera.position + camera.front;

    mat4 CLIP_FROM_WORLD = perspective(                         // NOTE: matrix calculation match with Vulkan
                               camera.camera_attributes.vfov,   // fov in radians
                               camera.camera_attributes.aspect, // aspect
                               camera.camera_attributes.near,   // near
                               camera.camera_attributes.far     // far
                               ) *
                           look_at(
                               camera.position[0], camera.position[1], camera.position[2],    // eye
                               target_direction[0], target_direction[1], target_direction[2], // target
                               camera.up[0], camera.up[1], camera.up[2]                       // up
                           );

    return glm::make_mat4(CLIP_FROM_WORLD.data());
}

glm::mat4 Camera_new::apply_scene_mode_camera(Camera_new &scene_camera)
{
    glm::mat4 CLIP_FROM_WORLD;

    auto findCameraNodeResult = s72_scene.nodes_map.find(scene_camera.name); // [WARNING] the camera CAMERA and NODE Object should always have the same name!
    if (findCameraNodeResult != s72_scene.nodes_map.end())
    {
        Node *cameraNode = findCameraNodeResult->second;

        glm::mat4 LOCAL_TO_WORLD;
        auto findCameraMatrixResult = s72_scene.transforms.find(cameraNode);
        if (findCameraMatrixResult != s72_scene.transforms.end())
        {
            // update CLIP_FROM_WORLD matrix based on current_scene_camera_object
            /* Thanks to Leon Li for helping me to correct my understanding of the CLIP_FROM_WORLD calculation formula (= perspective * WORLD_TO_LOCAL) for SCENE mode. */
            LOCAL_TO_WORLD = findCameraMatrixResult->second;
            scene_camera.update_camera_from_local_to_world(LOCAL_TO_WORLD); // update current_scene_camera_object parameters (vectors, eular angles, control status)
            CLIP_FROM_WORLD = calculate_camera_clip_from_world(scene_camera);
        }
        else
        {
            throw std::runtime_error("Scene camera named \"" + cameraNode->name + "\" matrix not found. Application exits.");
        }
    }

    return CLIP_FROM_WORLD;
}
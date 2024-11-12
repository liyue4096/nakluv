#pragma once

// #include "../Scene.hpp"
#include "Mat4.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stdint.h>
#include <string>
#include <cassert>

struct Camera_new
{
    Camera_new();
    ~Camera_new();
    // Camera_new(const Camera_new &) = delete;

    enum Camera_Mode : uint8_t
    {
        USER,
        SCENE,
        DEBUG
    };

    struct Camera_Attributes
    {
        float aspect;
        float vfov;
        float near;
        float far;
    } camera_attributes;

    std::string name;
    uint8_t camera_mode_cnt;
    Camera_Mode current_camera_mode;

    // =============================================
    // USER mode related variables

    /* cr. movement related handle learned from CMU 15666 Computer Game Programming code base
           https://github.com/15-466/15-466-f24-base2/blob/b7584e87b2498e4491e6438770f4b4a8d593bbde/PlayMode.cpp#L70 */

    struct Camera_Movement
    {
        bool left;
        bool right;
        bool up;
        bool down;
        bool forward;
        bool backward;
    } movements;

    struct Camera_Posture
    {
        bool yaw_left;
        bool yaw_right;
        bool pitch_up;
        bool pitch_down;
    } postures;

    struct Camera_Sensitivity
    {
        float kb_forward;
        float kb_upward;
        float kb_rightward;
        float kb_yaw;
        float kb_pitch;
        float mouse_yaw;
        float mouse_pitch;
        bool sensitivity_increase;
        bool sensitivity_decrease;
    } sensitivity;

    /* cr. camera parameters learned from Learn OpenGL
           https://learnopengl.com/Getting-started/Camera# */

    glm::vec3 position;
    glm::vec3 target_position;

    glm::vec3 front;
    glm::vec3 right;
    glm::vec3 up;

    float yaw;
    float pitch;
    float roll;

    float unit_angle;
    float unit_sensitivity;

    // =============================================
    // Helper Functions

    void reset_camera_control_status();
    void update_camera_eular_angles_from_vectors();
    void update_camera_vectors_from_eular_angles();
    void update_camera_from_local_to_world(glm::mat4 &localToWorld);

    void update_info_from_another_camera(const Camera_new &updateFrom);
    glm::mat4 calculate_camera_clip_from_world(Camera_new &camera);
    glm::mat4 apply_scene_mode_camera(Camera_new &camera);
};
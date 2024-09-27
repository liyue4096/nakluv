#include "PlayMode.hpp"

// #include "gl_errors.hpp"
// #include "data_path.hpp"
#include "SDL2/SDL.h"
#include "SDL2/SDL_mouse.h"
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <random>

extern S72_scene s72_scene;

PlayMode::PlayMode()
{
    camera_mode = s72_scene.camera_mode;
}

PlayMode::~PlayMode()
{
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size)
{

    if (evt.type == SDL_KEYDOWN)
    {
        if (evt.key.keysym.sym == SDLK_ESCAPE)
        {
            SDL_SetRelativeMouseMode(SDL_FALSE);
            return true;
        }
        else if (evt.key.keysym.sym == SDLK_a)
        {
            left.downs += 1;
            left.pressed = true;
            return true;
        }
        else if (evt.key.keysym.sym == SDLK_d)
        {
            right.downs += 1;
            right.pressed = true;
            return true;
        }
        else if (evt.key.keysym.sym == SDLK_w)
        {
            up.downs += 1;
            up.pressed = true;
            return true;
        }
        else if (evt.key.keysym.sym == SDLK_s)
        {
            down.downs += 1;
            down.pressed = true;
            return true;
        }
        else if (evt.key.keysym.sym == SDLK_1)
        {
            std::cout << "11111\n";
            button_1.downs += 1;
            camera_mode = SCENE;
            button_1.pressed = true;
            return true;
        }
        else if (evt.key.keysym.sym == SDLK_2)
        {
            button_2.downs += 1;
            camera_mode = USER;
            button_2.pressed = true;
            return true;
        }
        else if (evt.key.keysym.sym == SDLK_3)
        {
            button_3.downs += 1;
            camera_mode = DEBUG;
            button_3.pressed = true;
            return true;
        }
    }
    else if (evt.type == SDL_KEYUP)
    {
        if (evt.key.keysym.sym == SDLK_a)
        {
            left.pressed = false;
            return true;
        }
        else if (evt.key.keysym.sym == SDLK_d)
        {
            right.pressed = false;
            return true;
        }
        else if (evt.key.keysym.sym == SDLK_w)
        {
            up.pressed = false;
            return true;
        }
        else if (evt.key.keysym.sym == SDLK_s)
        {
            down.pressed = false;
            return true;
        }
        else if (evt.key.keysym.sym == SDLK_1)
        {
            button_1.pressed = false;
            return true;
        }
        else if (evt.key.keysym.sym == SDLK_2)
        {
            button_2.pressed = false;
            return true;
        }
        else if (evt.key.keysym.sym == SDLK_3)
        {
            button_3.pressed = false;
            return true;
        }
    }
    else if (evt.type == SDL_MOUSEBUTTONDOWN)
    {
        if (SDL_GetRelativeMouseMode() == SDL_FALSE)
        {
            SDL_SetRelativeMouseMode(SDL_TRUE);
            return true;
        }
    }
    else if (evt.type == SDL_MOUSEMOTION)
    {
        if (SDL_GetRelativeMouseMode() == SDL_TRUE && camera_ != nullptr)
        {
            glm::vec2 motion = glm::vec2(
                evt.motion.xrel / float(window_size.y),
                -evt.motion.yrel / float(window_size.y));

            Node *node_ = s72_scene.roots[camera_->name];

            node_->rotation = glm::normalize(
                node_->rotation * glm::angleAxis(-motion.x * camera_->perspective.vfov, glm::vec3(0.0f, 1.0f, 0.0f)) *
                glm::angleAxis(motion.y * camera_->perspective.vfov, glm::vec3(1.0f, 0.0f, 0.0f)));

            return true;
        }
    }

    return false;
}

void PlayMode::update(float elapsed)
{
    camera_ = s72_scene.current_camera_; // syn camera pointer
    if (camera_ == nullptr)
    {
        return;
    }

    Node *node_ = s72_scene.roots[camera_->name];

    // move camera:
    {
        // combine inputs into a move:
        constexpr float PlayerSpeed = 30.0f;
        glm::vec2 move = glm::vec2(0.0f);
        if (left.pressed && !right.pressed)
            move.x = -1.0f;
        if (!left.pressed && right.pressed)
            move.x = 1.0f;
        if (down.pressed && !up.pressed)
            move.y = -1.0f;
        if (!down.pressed && up.pressed)
            move.y = 1.0f;

        // make it so that moving diagonally doesn't go faster:
        if (move != glm::vec2(0.0f))
            move = glm::normalize(move) * PlayerSpeed * elapsed;

        glm::mat4x3 frame = node_->make_local_to_parent();
        glm::vec3 frame_right = frame[0];
        // glm::vec3 up = frame[1];
        glm::vec3 frame_forward = -frame[2];

        node_->position += move.x * frame_right + move.y * frame_forward;
    }

    // reset button press counters:
    left.downs = 0;
    right.downs = 0;
    up.downs = 0;
    down.downs = 0;
}
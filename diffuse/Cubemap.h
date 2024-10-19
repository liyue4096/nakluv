#pragma once
#include <vector>
#include <string>
#include "../include/stb/stb_image.h"
#include <glm/glm.hpp>

struct Cubemap
{
    std::string in_map_str = "";
    std::string out_map_str = "";

    int N = 16;
    int in_width, in_height;
    int out_width = N, out_height = N;

    std::vector<stbi_uc *> in_faces;
    std::vector<glm::vec3> in_normal;
    std::vector<stbi_uc *> out_faces;

    Cubemap();
    ~Cubemap();

    void work_flow(); // key fucntion

    void setup_in_faces();
    void setup_out_faces();
    void write_image();

    /* get_dir of a specific pixel
     * face: face index
     * row: cubemap face i row index
     * col: cubemap face i col index
     */
    glm::vec3 get_dir(int face, int row, int col, int width, int height);

    glm::vec3 sample_cubemap(int sample_face, int sample_row, int sample_col, int width, int height);

    // for a specific dir, make cos weight hemisphere_intergral
    glm::vec3 cos_hemisphere_intergral(glm::vec3 dir);

    glm::vec3 decodeRGBE(const unsigned char *rgbe);
    void encodeRGBE(glm::vec3 rgb, unsigned char *rgbe);

    void parse_args(int argc, char *argv[]);
};

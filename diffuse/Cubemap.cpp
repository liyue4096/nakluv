#define _CRT_SECURE_NO_WARNINGS
#include "Cubemap.h"

#include <iostream>
#include <algorithm>
#include <cassert>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../include/stb/stb_image_write.h"

Cubemap::Cubemap()
{
    in_map_str = "";
    out_map_str = "";

    in_faces.reserve(6);
    out_faces.reserve(6);

    in_normal = {glm::vec3(1., 0., 0.),
                 glm::vec3(-1., 0., 0.),
                 glm::vec3(0., 1., 0.),
                 glm::vec3(0., -1., 0.),
                 glm::vec3(0., 0., 1.),
                 glm::vec3(0., 0., -1.)};
}

Cubemap::~Cubemap()
{
    for (int i = 0; i < in_faces.size(); ++i)
    {
        if (in_faces[i])
        {
            stbi_image_free(in_faces[i]); // Free each image data
        }
    }

    for (int i = 0; i < out_faces.size(); ++i)
    {
        if (out_faces[i])
        {
            stbi_image_free(out_faces[i]); // Free each image data
        }
    }
}

void Cubemap::setup_in_faces()
{
    // read data from in_map_str
    int w, h, n, ok;
    std::string filename = "./resource/" + in_map_str;
    [[maybe_unused]] unsigned char *image_data = stbi_load(filename.c_str(), &w, &h, &n, 0);
    ok = stbi_info(filename.c_str(), &w, &h, &n);
    std::cout << "environment: " << filename.c_str() << " ok? " << ok << ": " << w << ", " << h << ", " << n << "\n";
    assert(n == 4);

    int per_h = h / 6;

    // set up in para
    in_width = w;
    in_height = per_h;

    // make up 6 faces
    for (int i = 0; i < 6; i++)
    {
        in_faces[i] = new stbi_uc[per_h * w * n];
        memcpy(in_faces[i], image_data + i * per_h * w * n, per_h * w * n);
    }
}

void Cubemap::write_image()
{
    std::string filename = "./resource/" + out_map_str;
    // for (int i = 0; i < 6; i++)
    {
        int rlt = stbi_write_png(filename.c_str(), out_width, out_height * 6, 4, out_faces[0], 4);
        std::cout << "write out image " << ": " << rlt << std::endl;
    }
}

void Cubemap::setup_out_faces()
{
    // make up 6 faces
    unsigned char *out = new stbi_uc[out_width * out_height * 6 * 4];
    for (int i = 0; i < 6; i++)
    {
        out_faces[i] = out + i * out_width * out_height * 4; // RGBE
    }
}

glm::vec3 Cubemap::get_dir(int face_index, int row, int col, int width, int height)
{
    glm::vec3 dir;

    // Convert pixel coordinates to range [0, 1]
    float u = (col + 0.5f) / width;
    float v = (row + 0.5f) / height;

    // Map [0, 1] to [-1, 1]
    float u_cubemap = 2.0f * u - 1.0f;
    float v_cubemap = 2.0f * v - 1.0f;

    switch (face_index)
    {
    case 0: // +X
        dir = glm::vec3(1.0f, -v_cubemap, -u_cubemap);
        break;
    case 1: // -X
        dir = glm::vec3(-1.0f, -v_cubemap, u_cubemap);
        break;
    case 2:                                           // +Y
        dir = glm::vec3(u_cubemap, 1.0f, -v_cubemap); // Y is up, flip v_cubemapz
        break;
    case 3: // -Y
        dir = glm::vec3(u_cubemap, -1.0f, v_cubemap);
        break;
    case 4: // +Z
        dir = glm::vec3(u_cubemap, -v_cubemap, 1.0f);
        break;
    case 5: // -Z
        dir = glm::vec3(-u_cubemap, -v_cubemap, -1.0f);
        break;
    default:
        // Invalid face index, return zero vector
        dir = glm::vec3(0.0f);
        break;
    }

    // Normalize the direction vector
    return glm::normalize(dir);
}

glm::vec3 Cubemap::cos_hemisphere_intergral(glm::vec3 dir)
{
    glm::vec3 result(0.0f);
    float weight_sum = 0.0f;

    for (int sample_face = 0; sample_face < 6; ++sample_face)
    {
        for (int sample_row = 0; sample_row < in_width; ++sample_row)
        {
            for (int sample_col = 0; sample_col < in_height; ++sample_col)
            {
                glm::vec3 sample_dir = get_dir(sample_face, sample_row, sample_col, in_width, in_height);

                // Calculate the weight: cos(theta) = max(dot(N, sample_dir), 0.0)
                float weight = glm::dot(glm::normalize(dir), glm::normalize(sample_dir));
                weight = std::max(weight, 0.0f); // Cosine-weight, only take positive contribution

                // Sample the input cubemap using sample_dir
                glm::vec3 sample_color = sample_cubemap(sample_face, sample_row, sample_col, in_width, in_height);

                // Compute the Jacobian factor
                float u = (sample_col + 0.5f) / in_width * 2.0f - 1.0f;  // Normalized to [-1, 1]
                float v = (sample_row + 0.5f) / in_height * 2.0f - 1.0f; // Normalized to [-1, 1]
                float jacobian = 1.0f / std::pow(1.0f + u * u + v * v, 1.5f);

                // Accumulate weighted color
                result += weight * sample_color * jacobian;
                weight_sum += weight * jacobian;
            }
        }
    }

    // Normalize the result by the total accumulated weight
    if (weight_sum > 0.0f)
    {
        // result /= weight_sum;
    }

    // Divide by Pi for Lambertian reflection
    result /= 3.14159f;

    return result;
}

glm::vec3 Cubemap::sample_cubemap(int sample_face, int sample_row, int sample_col, int width, int height)
{
    // Make sure sample_face is within the valid range (0-5 for cubemap faces)
    if (sample_face < 0 || sample_face >= 6)
    {
        return glm::vec3(0.0f); // Invalid face index
    }

    // Clamp row and column to be within the cubemap face resolution
    sample_row = std::clamp(sample_row, 0, width - 1);
    sample_col = std::clamp(sample_col, 0, height - 1);

    // Get the pixel data from the cubemap face (RGBE encoded)
    unsigned char *pixel = &in_faces[sample_face][(sample_row * width + sample_col) * 4]; // RGBE, 4 channels

    // Decode the RGBE data into linear RGB
    return decodeRGBE(pixel);
}

glm::vec3 Cubemap::decodeRGBE(const unsigned char *rgbe)
{
    // Extract RGB components and the exponent (E is stored in the alpha channel)
    int exponent = rgbe[3] - 128; // The exponent is stored with an offset of 128
    if (exponent == -128)
    {
        // Special case: if exponent is -128, this pixel is black
        return glm::vec3(0.0f, 0.0f, 0.0f);
    }

    // Scale factor based on the exponent
    float scale = std::ldexp(1.0f, exponent); // ldexp(x, exp) is equivalent to x * 2^exp

    // Convert the RGBE values to floating-point RGB
    return glm::vec3(
        (rgbe[0] + 0.5f / 256.0f) * scale,
        (rgbe[1] + 0.5f / 255.0f) * scale,
        (rgbe[2] + 0.5f / 255.0f) * scale);
}

void Cubemap::encodeRGBE(glm::vec3 rgb, unsigned char *rgbe)
{
    float max_component = std::max(std::max(rgb.r, rgb.g), rgb.b);

    if (max_component < 1e-32)
    {
        // Special case for very small values (black pixel)
        rgbe[0] = rgbe[1] = rgbe[2] = rgbe[3] = 0;
    }
    else
    {
        // Compute exponent and mantissa for RGBE format
        int exponent;
        float scale = frexp(max_component, &exponent) * 256.0f / max_component;

        rgbe[0] = static_cast<unsigned char>(rgb.r * scale);
        rgbe[1] = static_cast<unsigned char>(rgb.g * scale);
        rgbe[2] = static_cast<unsigned char>(rgb.b * scale);
        rgbe[3] = static_cast<unsigned char>(exponent + 128); // Store the exponent with an offset of 128
    }
}

void Cubemap::work_flow()
{
    setup_in_faces();

    setup_out_faces();

    for (int face_index = 0; face_index < 6; ++face_index)
    {
        for (int row = 0; row < out_width; ++row)
        {
            for (int col = 0; col < out_height; ++col)
            {
                auto out_dir = get_dir(face_index, row, col, out_width, out_height);
                auto out_pixel = cos_hemisphere_intergral(out_dir);

                unsigned char *out_pixel_location = out_faces[face_index] + (row * out_height + col) * 4;

                encodeRGBE(out_pixel, out_pixel_location);
            }
        }
    }

    write_image();
}

void Cubemap::parse_args(int argc, char *argv[])
{
    // Check that the correct number of arguments are passed
    if (argc != 4)
    {
        std::cerr << "Usage: " << argv[0] << " <input_file> --lambertian <output_file>\n";
        exit(EXIT_FAILURE);
    }

    // Set the input and output file names based on the command line arguments
    in_map_str = argv[1];

    // Check if the second argument is "--lambertian"
    std::string option = argv[2];
    if (option != "--lambertian")
    {
        std::cerr << "Error: Expected '--lambertian' option, but got '" << option << "'\n";
        exit(EXIT_FAILURE);
    }

    // Set the output filename
    out_map_str = argv[3];
}
#include "RTG.hpp"

#include "Tutorial.hpp"
#include "diffuse/Cubemap.h"

#include <iostream>
#include <chrono>

int main(int argc, char **argv)
{
    auto start = std::chrono::high_resolution_clock::now();

    Cubemap cubemap;

    cubemap.parse_args(argc, argv);

    cubemap.work_flow();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = (end - start) * 1000;
    std::cout << "render time: " << elapsed.count() << " ms" << std::endl;

    return 0;
}
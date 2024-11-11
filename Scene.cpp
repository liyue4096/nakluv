#include "Scene.hpp"

// Define the global variable
S72_scene s72_scene;

// read s72 file
void get_scene(const std::vector<sejp::value> &array)
{
    // std::cout << "\narray size(): " << array.size() << "\n";
    int index = 0;
    bool index_roots = false;

    for (const auto &item : array)
    {
        // std::cout << i << ", ";
        if (!item.as_object().has_value())
            continue;

        const auto &obj = item.as_object().value();

        if (auto type_opt = obj.find("type"); type_opt != obj.end())
        {
            // Check if the type is "SCENE"
            if (type_opt->second.as_string().value() == "SCENE")
            {
                // Get the "name" field
                if (auto name_opt = obj.find("name"); name_opt != obj.end())
                {
                    if (name_opt->second.as_string())
                    {
                        s72_scene.scene.name = name_opt->second.as_string().value();
                        // std::cout << "\nScene name: " << Scene.name;
                    }
                }

                if (auto roots_opt = obj.find("roots"); roots_opt != obj.end())
                {
                    if (roots_opt->second.as_array())
                    {
                        const auto &roots_array = roots_opt->second.as_array().value();
                        s72_scene.scene.roots.clear(); // Clear any previous data

                        // Store roots as either strings or numbers
                        for (const auto &root : roots_array)
                        {
                            if (root.as_string())
                            {
                                // std::cout << "\nstring:";
                                s72_scene.scene.roots.push_back(root.as_string().value());
                            }
                            else if (root.as_number())
                            {
                                // std::cout << "\nnumber:";
                                s72_scene.scene.roots.push_back(root.as_number().value());
                                index_roots = true;
                            }
                        }
                    }
                }
            }

            // parse msg to Node
            else if (type_opt->second.as_string().value() == "NODE")
            {
                Node node;

                // Get "name" field
                if (auto name_opt = obj.find("name"); name_opt != obj.end() && name_opt->second.as_string())
                {
                    node.name = name_opt->second.as_string().value();
                }

                // Get "translation" field
                if (auto translation_opt = obj.find("translation"); translation_opt != obj.end() && translation_opt->second.as_array())
                {
                    const auto &translation_array = translation_opt->second.as_array().value();
                    if (translation_array.size() == 3)
                    {
                        // node.Translation.tx = (float)translation_array[0].as_number().value_or(0.f);
                        // node.Translation.ty = (float)translation_array[1].as_number().value_or(0.f);
                        // node.Translation.tz = (float)translation_array[2].as_number().value_or(0.f);
                        node.position = glm::vec3((float)translation_array[0].as_number().value_or(0.f),
                                                  (float)translation_array[1].as_number().value_or(0.f),
                                                  (float)translation_array[2].as_number().value_or(0.f));
                    }
                }

                // Get "rotation" field
                if (auto rotation_opt = obj.find("rotation"); rotation_opt != obj.end() && rotation_opt->second.as_array())
                {
                    const auto &rotation_array = rotation_opt->second.as_array().value();
                    if (rotation_array.size() == 4)
                    {
                        // node.Rotation.rx = (float)rotation_array[0].as_number().value_or(0.f);
                        // node.Rotation.ry = (float)rotation_array[1].as_number().value_or(0.f);
                        // node.Rotation.rz = (float)rotation_array[2].as_number().value_or(0.f);
                        // node.Rotation.rw = (float)rotation_array[3].as_number().value_or(1.f);
                        node.rotation = glm::quat((float)rotation_array[3].as_number().value_or(1.f),
                                                  (float)rotation_array[0].as_number().value_or(1.f),
                                                  (float)rotation_array[1].as_number().value_or(1.f),
                                                  (float)rotation_array[2].as_number().value_or(1.f));
                    }
                }

                // Get "scale" field
                if (auto scale_opt = obj.find("scale"); scale_opt != obj.end() && scale_opt->second.as_array())
                {
                    const auto &scale_array = scale_opt->second.as_array().value();
                    if (scale_array.size() == 3)
                    {
                        node.scale = glm::vec3((float)scale_array[0].as_number().value_or(0.f),
                                               (float)scale_array[1].as_number().value_or(0.f),
                                               (float)scale_array[2].as_number().value_or(0.f));
                    }
                }

                if (auto children_opt = obj.find("children"); children_opt != obj.end() && children_opt->second.as_array())
                {
                    const auto &children_array = children_opt->second.as_array().value();
                    for (const auto &child : children_array)
                    {
                        if (child.as_string())
                        {
                            node.children.push_back(child.as_string().value());
                        }
                        else if (child.as_number())
                        {
                            // std::cout << "\nnumber:";
                            node.children.push_back(child.as_number().value());
                        }
                    }
                }

                // Get optional fields like mesh, camera, environment, and light
                if (auto mesh_opt = obj.find("mesh"); mesh_opt != obj.end() && mesh_opt->second.as_string())
                {
                    node.mesh_name = mesh_opt->second.as_string().value();
                }

                if (auto mesh_opt = obj.find("camera"); mesh_opt != obj.end() && mesh_opt->second.as_string())
                {
                    node.camera_name = mesh_opt->second.as_string().value();
                }

                if (auto environment_opt = obj.find("environment"); environment_opt != obj.end() && environment_opt->second.as_string())
                {
                    node.environment_name = environment_opt->second.as_string().value();
                }

                if (auto light_opt = obj.find("light"); light_opt != obj.end() && light_opt->second.as_string())
                {
                    node.light_name = light_opt->second.as_string().value();
                }

                // Add the parsed node to the nodes vector
                s72_scene.nodes.push_back(node);
            }

            // parse msg to Mesh
            else if (type_opt->second.as_string().value() == "MESH")
            {
                Mesh mesh;

                // Get "name" field
                if (auto name_opt = obj.find("name"); name_opt != obj.end() && name_opt->second.as_string())
                {
                    mesh.name = name_opt->second.as_string().value();
                }

                // Get "topology" field
                if (auto topology_opt = obj.find("topology"); topology_opt != obj.end() && topology_opt->second.as_string())
                {
                    mesh.topology = topology_opt->second.as_string().value();
                }

                // Get "count" field
                if (auto count_opt = obj.find("count"); count_opt != obj.end() && count_opt->second.as_number())
                {
                    mesh.count = static_cast<uint32_t>(count_opt->second.as_number().value_or(0));
                }

                // Get "indices" field (optional)
                if (auto indices_opt = obj.find("indices"); indices_opt != obj.end() && indices_opt->second.as_object())
                {
                    const auto &indices_obj = indices_opt->second.as_object().value();

                    if (auto src_opt = indices_obj.find("src"); src_opt != indices_obj.end() && src_opt->second.as_string())
                    {
                        mesh.Indices.src = src_opt->second.as_string().value();
                    }

                    if (auto offset_opt = indices_obj.find("offset"); offset_opt != indices_obj.end() && offset_opt->second.as_number())
                    {
                        mesh.Indices.offset = static_cast<uint32_t>(offset_opt->second.as_number().value_or(0));
                    }

                    if (auto format_opt = indices_obj.find("format"); format_opt != indices_obj.end() && format_opt->second.as_string())
                    {
                        mesh.Indices.format = format_opt->second.as_string().value();
                    }
                }

                // Get "attributes" field (required)
                if (auto attributes_opt = obj.find("attributes"); attributes_opt != obj.end() && attributes_opt->second.as_object())
                {
                    const auto &attributes_obj = attributes_opt->second.as_object().value();

                    for (const auto &attribute_pair : attributes_obj)
                    {
                        const std::string &attribute_name = attribute_pair.first;
                        const auto &attribute_value = attribute_pair.second;

                        if (attribute_value.as_object())
                        {
                            const auto &attr_obj = attribute_value.as_object().value();
                            Mesh::Attribute attr;

                            // Get "src" for attribute
                            if (auto src_opt = attr_obj.find("src"); src_opt != attr_obj.end() && src_opt->second.as_string())
                            {
                                attr.src = src_opt->second.as_string().value();
                            }

                            // Get "offset" for attribute
                            if (auto offset_opt = attr_obj.find("offset"); offset_opt != attr_obj.end() && offset_opt->second.as_number())
                            {
                                attr.offset = static_cast<uint32_t>(offset_opt->second.as_number().value_or(0));
                            }

                            // Get "stride" for attribute
                            if (auto stride_opt = attr_obj.find("stride"); stride_opt != attr_obj.end() && stride_opt->second.as_number())
                            {
                                attr.stride = static_cast<uint32_t>(stride_opt->second.as_number().value_or(0));
                            }

                            // Get "format" for attribute
                            if (auto format_opt = attr_obj.find("format"); format_opt != attr_obj.end() && format_opt->second.as_string())
                            {
                                attr.format = format_opt->second.as_string().value();
                            }

                            // Add the parsed attribute to the attributes map
                            mesh.attributes[attribute_name] = attr;
                        }
                    }
                }

                // Get "material" field (optional)
                if (auto material_opt = obj.find("material"); material_opt != obj.end() && material_opt->second.as_string())
                {
                    mesh.material = material_opt->second.as_string().value();
                }

                // Add the parsed mesh to the meshes vector
                s72_scene.meshes.push_back(mesh);
            }

            // parse msg to Camera
            else if (type_opt->second.as_string().value() == "CAMERA")
            {
                Camera camera;

                // Get "name" field
                if (auto name_opt = obj.find("name"); name_opt != obj.end() && name_opt->second.as_string())
                {
                    camera.name = name_opt->second.as_string().value();
                }

                // Get "perspective" field
                if (auto perspective_opt = obj.find("perspective"); perspective_opt != obj.end() && perspective_opt->second.as_object())
                {
                    const auto &perspective_obj = perspective_opt->second.as_object().value();

                    // Get "aspect" field
                    if (auto aspect_opt = perspective_obj.find("aspect"); aspect_opt != perspective_obj.end() && aspect_opt->second.as_number())
                    {
                        camera.perspective.aspect = static_cast<float>(aspect_opt->second.as_number().value_or(0.f));
                    }

                    // Get "vfov" field
                    if (auto vfov_opt = perspective_obj.find("vfov"); vfov_opt != perspective_obj.end() && vfov_opt->second.as_number())
                    {
                        camera.perspective.vfov = static_cast<float>(vfov_opt->second.as_number().value_or(0.f));
                    }

                    // Get "near" field
                    if (auto near_opt = perspective_obj.find("near"); near_opt != perspective_obj.end() && near_opt->second.as_number())
                    {
                        camera.perspective.near = static_cast<float>(near_opt->second.as_number().value_or(0.f));
                    }

                    // Get "far" field
                    if (auto far_opt = perspective_obj.find("far"); far_opt != perspective_obj.end() && far_opt->second.as_number())
                    {
                        camera.perspective.far = static_cast<float>(far_opt->second.as_number().value_or(0.f));
                    }
                }

                // Add the parsed camera to the cameras vector
                s72_scene.cameras.push_back(camera);
            }

            // parse msg to Driver
            else if (type_opt->second.as_string().value() == "DRIVER")
            {
                Driver driver;

                // Get "name" field
                if (auto name_opt = obj.find("name"); name_opt != obj.end() && name_opt->second.as_string())
                {
                    driver.name = name_opt->second.as_string().value();
                }
                // Get "node" field
                if (auto name_opt = obj.find("node"); name_opt != obj.end() && name_opt->second.as_string())
                {
                    driver.refnode_name = name_opt->second.as_string().value();
                }
                // Get "channel" field
                if (auto name_opt = obj.find("channel"); name_opt != obj.end() && name_opt->second.as_string())
                {
                    std::string channel_str = name_opt->second.as_string().value();
                    if (channel_str == "translation")
                    {
                        driver.channel = DriverChannleType::TRANSLATION;
                        driver.channel_dim = 3;
                    }
                    if (channel_str == "scale")
                    {
                        driver.channel = DriverChannleType::SCALE;
                        driver.channel_dim = 3;
                    }
                    if (channel_str == "rotation")
                    {
                        driver.channel = DriverChannleType::ROTATION;
                        driver.channel_dim = 4;
                    }
                }
                // Get "frames" field by combining "times" and "values"
                if (auto times_opt = obj.find("times"); times_opt != obj.end() && times_opt->second.as_array())
                {
                    auto &timesArray = times_opt->second.as_array().value();
                    if (auto values_opt = obj.find("values"); values_opt != obj.end() && values_opt->second.as_array())
                    {
                        auto &valuesArray = values_opt->second.as_array().value();
                        float dt = 0.f;
                        // Check if the values array size is correct: timesArray.size() * channel_dim == valuesArray.size()
                        if (timesArray.size() * driver.channel_dim == valuesArray.size())
                        {
                            for (size_t i = 0; i < timesArray.size(); ++i)
                            {
                                if (!timesArray[i].as_number())
                                    continue;

                                Driver::Frame frame;
                                frame.time = static_cast<float>(timesArray[i].as_number().value());
                                // std::cout << frame.time << ":  ";
                                //  Extract channel_dim values for each frame
                                for (size_t j = 0; j < driver.channel_dim; ++j)
                                {
                                    if (valuesArray[i * driver.channel_dim + j].as_number())
                                    {
                                        frame.value.push_back(static_cast<float>(valuesArray[i * driver.channel_dim + j].as_number().value()));
                                    }
                                    // std::cout << frame.value[j] << ",";
                                }
                                // std::cout << "    ";

                                driver.frames.push_back(frame);

                                if (frame.time > s72_scene.animation_duration)
                                {
                                    dt = frame.time - s72_scene.animation_duration;
                                    s72_scene.animation_duration = frame.time;
                                }
                            }
                            // for the last frame interpolation in loop animation
                            s72_scene.animation_duration += dt;
                        }
                    }
                }
                if (auto name_opt = obj.find("interpolation"); name_opt != obj.end() && name_opt->second.as_string())
                {

                    std::string interpolationStr = name_opt->second.as_string().value();
                    if (interpolationStr == "STEP")
                    {
                        driver.interpolation = DriverInterpolation::STEP;
                        // std::cout << "interpolation " << driverObject->interpolation << std::endl; // [PASS]
                    }
                    if (interpolationStr == "LINEAR")
                    {
                        driver.interpolation = DriverInterpolation::LINEAR;
                        // std::cout << "interpolation " << driverObject->interpolation << std::endl; // [PASS]
                    }
                    if (interpolationStr == "SLERP")
                    {
                        driver.interpolation = DriverInterpolation::SLERP;
                        // std::cout << "interpolation " << driverObject->interpolation << std::endl; // [PASS]
                    }
                }
                // Add the parsed driver to the drivers vector
                s72_scene.drivers.push_back(driver);
            }
            // parse msg to MATERIAL
            else if (type_opt->second.as_string().value() == "MATERIAL")
            {
                MaterialObject material;

                // Get "name" field
                if (auto name_opt = obj.find("name"); name_opt != obj.end() && name_opt->second.as_string())
                {
                    material.name = name_opt->second.as_string().value();
                    // printf("material.name %s\n", material.name.c_str());
                }

                // Get "normalMap" field
                if (auto normalmap_opt = obj.find("normalMap"); normalmap_opt != obj.end() && normalmap_opt->second.as_object())
                {
                    auto &normalmap_obj = normalmap_opt->second.as_object().value();

                    if (auto src_opt = normalmap_obj.find("src"); src_opt != normalmap_obj.end() && src_opt->second.as_string())
                    {
                        material.normalmap = Texture{src_opt->second.as_string().value()};
                        s72_scene.textures_src.push_back(src_opt->second.as_string().value());
                        // printf("normal map %s\n", s72_scene.textures_src.back().c_str());
                    }
                }

                // Get "displacementMap" field
                if (auto displacementmap_opt = obj.find("displacementMap"); displacementmap_opt != obj.end() && displacementmap_opt->second.as_object())
                {
                    auto &isplacementmap_obj = displacementmap_opt->second.as_object().value();
                    if (auto src_opt = isplacementmap_obj.find("src"); src_opt != isplacementmap_obj.end() && src_opt->second.as_string())
                    {
                        material.displacementmap = Texture{src_opt->second.as_string().value()};
                        s72_scene.textures_src.push_back(src_opt->second.as_string().value());
                    }
                }

                // Parse material type (PBR, Lambertian, Mirror, Environment)
                if (auto pbr_opt = obj.find("pbr"); pbr_opt != obj.end() && pbr_opt->second.as_object())
                {
                    material.type = MaterialType::PBR;
                    PBRMaterial pbrMaterial;

                    const auto &pbr_obj = pbr_opt->second.as_object().value();
                    // Parse "albedo" field
                    if (auto albedo_opt = pbr_obj.find("albedo"); albedo_opt != pbr_obj.end())
                    {
                        const auto &albedo_obj = albedo_opt->second;
                        if (albedo_obj.as_array())
                        {
                            auto &albedoArray = albedo_obj.as_array().value();
                            if (albedoArray.size() == 3 && albedoArray[0].as_number() && albedoArray[1].as_number() && albedoArray[2].as_number())
                            {
                                pbrMaterial.albedo = glm::vec3(
                                    albedoArray[0].as_number().value(),
                                    albedoArray[1].as_number().value(),
                                    albedoArray[2].as_number().value());
                            }
                        }
                        else if (albedo_opt->second.as_object())
                        {
                            const auto &albedo_obj_val = albedo_obj.as_object().value();
                            if (auto src_opt = albedo_obj_val.find("src"); src_opt != albedo_obj_val.end() && src_opt->second.as_string())
                            {
                                pbrMaterial.albedo = Texture{src_opt->second.as_string().value()};
                                s72_scene.textures_src.push_back(src_opt->second.as_string().value());
                            }
                        }
                    }

                    // Parse "roughness" field
                    if (auto roughness_opt = pbr_obj.find("roughness"); roughness_opt != pbr_obj.end())
                    {
                        const auto &roughness_obj = roughness_opt->second;
                        if (roughness_obj.as_number())
                        {
                            pbrMaterial.roughness = (float)roughness_obj.as_number().value();
                        }
                        else if (roughness_obj.as_object())
                        {
                            const auto &roughness_obj_val = roughness_obj.as_object().value();
                            if (auto src_opt = roughness_obj_val.find("src"); src_opt != roughness_obj_val.end() && src_opt->second.as_string())
                            {
                                pbrMaterial.roughness = Texture{src_opt->second.as_string().value()};
                                s72_scene.textures_src.push_back(src_opt->second.as_string().value());
                            }
                        }
                    }

                    // Parse "metalness" field
                    if (auto metalness_opt = pbr_obj.find("metalness"); metalness_opt != pbr_obj.end())
                    {
                        const auto &metalness_obj = metalness_opt->second;
                        if (metalness_obj.as_number())
                        {
                            pbrMaterial.metalness = (float)metalness_obj.as_number().value();
                        }
                        else if (metalness_obj.as_object())
                        {
                            const auto &metalness_obj_val = metalness_obj.as_object().value();
                            if (auto src_opt = metalness_obj_val.find("src"); src_opt != metalness_obj_val.end() && src_opt->second.as_string())
                            {
                                pbrMaterial.metalness = Texture{src_opt->second.as_string().value()};
                                s72_scene.textures_src.push_back(src_opt->second.as_string().value());
                            }
                        }
                    }

                    material.material = pbrMaterial;
                }
                else if (auto lambertian_opt = obj.find("lambertian"); lambertian_opt != obj.end() && lambertian_opt->second.as_object())
                {
                    material.type = MaterialType::LAMBERTIAN;
                    LambertianMaterial lambertianMaterial;

                    const auto &lambertian_obj = lambertian_opt->second.as_object().value();

                    // Parse "albedo" field
                    if (auto albedo_opt = lambertian_obj.find("albedo"); albedo_opt != lambertian_obj.end())
                    {
                        const auto &albedo_obj = albedo_opt->second;
                        if (albedo_obj.as_array())
                        {
                            auto &albedoArray = albedo_obj.as_array().value();
                            if (albedoArray.size() == 3 && albedoArray[0].as_number() && albedoArray[1].as_number() && albedoArray[2].as_number())
                            {
                                lambertianMaterial.albedo = glm::vec3(
                                    albedoArray[0].as_number().value(),
                                    albedoArray[1].as_number().value(),
                                    albedoArray[2].as_number().value());
                            }
                        }
                        else if (albedo_obj.as_object())
                        {
                            const auto &albedo_obj_val = albedo_obj.as_object().value();
                            if (auto src_opt = albedo_obj_val.find("src"); src_opt != albedo_obj_val.end() && src_opt->second.as_string())
                            {
                                lambertianMaterial.albedo = Texture{src_opt->second.as_string().value()};
                                s72_scene.textures_src.push_back(src_opt->second.as_string().value());
                            }
                        }
                    }

                    material.material = lambertianMaterial;
                }
                else if (auto mirror_opt = obj.find("mirror"); mirror_opt != obj.end() && mirror_opt->second.as_object())
                {
                    material.type = MaterialType::MIRROR;
                    // Mirror material does not have parameters in the provided example
                    material.material = std::monostate{};
                }
                else if (auto environment_opt = obj.find("environment"); environment_opt != obj.end() && environment_opt->second.as_object())
                {
                    material.type = MaterialType::ENVIRONMENT;
                    // Environment material does not have parameters in the provided example
                    material.material = std::monostate{};
                }

                // Add the parsed material to the materials vector
                s72_scene.materials.push_back(material);
            }

            // parse msg to Environment
            else if (type_opt->second.as_string().value() == "ENVIRONMENT")
            {
                // Environment env;
                //  Get "name" field
                if (auto name_opt = obj.find("name"); name_opt != obj.end() && name_opt->second.as_string())
                {
                    s72_scene.environment.name = name_opt->second.as_string().value();
                }
                // Get "radiance" field
                if (auto name_opt = obj.find("radiance"); name_opt != obj.end() && name_opt->second.as_object())
                {
                    const auto &channel_obj = name_opt->second.as_object().value();
                    if (auto envir_src_opt = channel_obj.find("src"); envir_src_opt != channel_obj.end() && envir_src_opt->second.as_string())
                    {
                        s72_scene.environment.radiance.src = envir_src_opt->second.as_string().value();
                        // s72_scene.textures_src.push_back(s72_scene.environment.radiance.src);
                    }
                    if (auto envir_type_opt = channel_obj.find("type"); envir_type_opt != channel_obj.end() && envir_type_opt->second.as_string())
                    {
                        s72_scene.environment.radiance.type = envir_type_opt->second.as_string().value();
                    }
                    if (auto opt = channel_obj.find("format"); opt != channel_obj.end() && opt->second.as_string())
                    {
                        s72_scene.environment.radiance.format = opt->second.as_string().value();
                    }
                }
            }
            // parse msg to Environment
            else if (type_opt->second.as_string().value() == "LIGHT")
            {
                LightObject light;
                //  Get "name" field
                if (auto name_opt = obj.find("name"); name_opt != obj.end() && name_opt->second.as_string())
                {
                    light.name = name_opt->second.as_string().value();
                }
                //  Get "shadow" field
                if (auto shadow_opt = obj.find("shadow"); shadow_opt != obj.end() && shadow_opt->second.as_number())
                {
                    light.shadow = static_cast<uint32_t>(shadow_opt->second.as_number().value_or(0.f));
                }
                // Check for "sun" type light
                if (auto sun_opt = obj.find("sun"); sun_opt != obj.end() && sun_opt->second.as_object())
                {
                    light.type = SUN;
                    const auto &sun = sun_opt->second.as_object().value();
                    if (auto angle_opt = sun.find("angle"); angle_opt != sun.end() && angle_opt->second.as_number())
                    {
                        light.data.sun.angle = static_cast<float>(angle_opt->second.as_number().value_or(0.0f));
                    }
                    if (auto strength_opt = sun.find("strength"); strength_opt != sun.end() && strength_opt->second.as_number())
                    {
                        light.data.sun.strength = static_cast<float>(strength_opt->second.as_number().value_or(0.0f));
                    }
                }
                // Check for "sphere" type light
                else if (auto sphere_opt = obj.find("sphere"); sphere_opt != obj.end() && sphere_opt->second.as_object())
                {
                    light.type = SPHERE;
                    const auto &sphere = sphere_opt->second.as_object().value();
                    if (auto radius_opt = sphere.find("radius"); radius_opt != sphere.end() && radius_opt->second.as_number())
                    {
                        light.data.sphere.radius = static_cast<float>(radius_opt->second.as_number().value_or(0.0f));
                    }
                    if (auto power_opt = sphere.find("power"); power_opt != sphere.end() && power_opt->second.as_number())
                    {
                        light.data.sphere.power = static_cast<float>(power_opt->second.as_number().value_or(0.0f));
                    }
                    if (auto limit_opt = sphere.find("limit"); limit_opt != sphere.end() && limit_opt->second.as_number())
                    {
                        light.data.sphere.limit = static_cast<float>(limit_opt->second.as_number().value_or(0.0f));
                    }
                }
                // Check for "spot" type light
                else if (auto spot_opt = obj.find("spot"); spot_opt != obj.end() && spot_opt->second.as_object())
                {
                    light.type = SPOT;
                    const auto &spot = spot_opt->second.as_object().value();
                    if (auto radius_opt = spot.find("radius"); radius_opt != spot.end() && radius_opt->second.as_number())
                    {
                        light.data.spot.radius = static_cast<float>(radius_opt->second.as_number().value_or(0.0f));
                    }
                    if (auto power_opt = spot.find("power"); power_opt != spot.end() && power_opt->second.as_number())
                    {
                        light.data.spot.power = static_cast<float>(power_opt->second.as_number().value_or(0.0f));
                    }
                    if (auto fov_opt = spot.find("fov"); fov_opt != spot.end() && fov_opt->second.as_number())
                    {
                        light.data.spot.fov = static_cast<float>(fov_opt->second.as_number().value_or(0.0f));
                    }
                    if (auto blend_opt = spot.find("blend"); blend_opt != spot.end() && blend_opt->second.as_number())
                    {
                        light.data.spot.blend = static_cast<float>(blend_opt->second.as_number().value_or(0.0f));
                    }
                    if (auto limit_opt = spot.find("limit"); limit_opt != spot.end() && limit_opt->second.as_number())
                    {
                        light.data.spot.limit = static_cast<float>(limit_opt->second.as_number().value_or(0.0f));
                    }
                }

                s72_scene.lights.push_back(light);
            }

            else if (type_opt->second.as_string().value() == "PTERRAIN")
            {

                // Get "name" field
                if (auto name_opt = obj.find("name"); name_opt != obj.end() && name_opt->second.as_string())
                {
                    s72_scene.terrain.name = name_opt->second.as_string().value();
                }
                // Get "length" field
                if (auto length_opt = obj.find("length"); length_opt != obj.end() && length_opt->second.as_number())
                {
                    s72_scene.terrain.length = static_cast<uint32_t>(length_opt->second.as_number().value_or(0));
                }
                // Get "blocksize" field
                if (auto blocksize_opt = obj.find("count"); blocksize_opt != obj.end() && blocksize_opt->second.as_number())
                {
                    s72_scene.terrain.block_size = static_cast<uint32_t>(blocksize_opt->second.as_number().value_or(0));
                }
                if (auto control_image_opt = obj.find("controlImage"); control_image_opt != obj.end() && control_image_opt->second.as_string())
                {
                    s72_scene.terrain.control_image = control_image_opt->second.as_string().value();
                }
                if (auto noise_source_opt = obj.find("noiseSource"); noise_source_opt != obj.end() && noise_source_opt->second.as_string())
                {
                    s72_scene.terrain.name = noise_source_opt->second.as_string().value();
                }
            }
        }

        index++;
    }
}

Mesh *find_mesh_by_name(const std::string &mesh_name)
{
    for (auto &mesh : s72_scene.meshes)
    {
        if (mesh.name == mesh_name)
        {
            return &mesh; // Return a pointer to the matching mesh
        }
    }
    return nullptr; // Return nullptr if mesh is not found
}

Camera *find_camera_by_name(const std::string &camera_name)
{
    for (auto &camera : s72_scene.cameras)
    {
        if (camera.name == camera_name)
        {
            return &camera; // Return a pointer to the matching camera
        }
    }
    return nullptr; // Return nullptr if camera is not found
}

LightObject *find_light_by_name(const std::string &light_name)
{
    for (auto &light : s72_scene.lights)
    {
        if (light.name == light_name)
        {
            return &light; // Return a pointer to the matching light
        }
    }
    return nullptr; // Return nullptr if light is not found
}

Node *find_node_by_name(std::string &str)
{
    assert(str != "");
    assert(s72_scene.nodes_map.find(str) != s72_scene.nodes_map.end());

    return s72_scene.nodes_map[str];
}

Node *find_node_by_name_or_index(const std::variant<std::string, double> &root)
{
    if (std::holds_alternative<std::string>(root))
    {
        const std::string &node_name = std::get<std::string>(root);
        for (auto &node : s72_scene.nodes)
        {
            if (node.name == node_name)
            {
                return &node;
            }
        }
    }
    else if (std::holds_alternative<double>(root))
    {
        int node_index = static_cast<int>(std::get<double>(root)); // Convert double to int index

        // Additional checks to ensure node_index is valid
        if (node_index < 0 || node_index >= static_cast<int>(s72_scene.nodes.size()))
        {
            std::cerr << "Error: node_index " << node_index
                      << " is out of bounds! Valid range is [0, "
                      << s72_scene.nodes.size() - 1 << "]\n";
            return nullptr;
        }
        assert(node_index >= 0 && node_index < static_cast<int>(s72_scene.nodes.size()));
        return &s72_scene.nodes[node_index];
    }
    return nullptr; // Node not found
}

// DFS to build the node tree
void dfs_build_tree(Node *current_node, Node *parrent_node, std::vector<Node *> &current_path)
{
    if (!current_node)
        return;

    current_node->parent_ = parrent_node;

    // Add the current node to the path
    current_path.push_back(current_node);

    // Bind the mesh if the node has a valid mesh_name
    if (!current_node->mesh_name.empty())
    {
        current_node->mesh_ = find_mesh_by_name(current_node->mesh_name);
    }

    // Bind the camera if the node has a valid camera_name
    if (!current_node->camera_name.empty())
    {
        current_node->camera_ = find_camera_by_name(current_node->camera_name);
        // std::cout << "Node: " << current_node->name << " has a camera :" << current_node->camera_->name << "\n";
    }

    // If the current node has a camera, store the path in the cameras_path map
    if (current_node->camera_ != nullptr)
    {
        // Print the path (node names)
        // std::cout << "Path to camera " << current_node->camera_->name << ": ";
        // for (const auto &node_in_path : current_path)
        // {
        //     std::cout << node_in_path->name << " -> "; // Print each node name followed by an arrow
        // }
        // std::cout << "[End]" << std::endl; // Mark the end of the path

        s72_scene.cameras_path[current_node->camera_->name] = current_path;
    }

    // Bind the light if the node has a valid camera_name
    if (!current_node->light_name.empty())
    {
        current_node->light_ = find_light_by_name(current_node->light_name);
        // std::cout << "Node: " << current_node->name << " has a camera :" << current_node->camera_->name << "\n";
        if (current_node->light_)
        {
            s72_scene.light_node_map[current_node->light_].emplace_back(current_node);
        }
    }

    // Process each child of the current node
    for (const auto &child : current_node->children)
    {
        Node *child_node = find_node_by_name_or_index(child);

        if (child_node)
        {
            current_node->children_node_.push_back(child_node);
            // std::cout << " child: " << child_node->name << "  mesh: " << child_node->mesh_name << " to parent: " << current_node->name << "\n";
            //   Recursively build the tree for the child
            //  s72_scene.nodes_map.push_back(child_node); // Store in nodes_map
            s72_scene.nodes_map[child_node->name] = child_node;
            dfs_build_tree(child_node, current_node, current_path); // Continue DFS
        }
    }
    // std::cout << current_node->name << " children #:  " << current_node->children_node_.size() << "\n";

    // Remove the current node from the path after processing all children
    current_path.pop_back();
}

void build_node_trees()
{
    s72_scene.nodes_map.clear();
    s72_scene.cameras_path.clear();

    for (auto &root : s72_scene.scene.roots)
    {
        if (std::holds_alternative<std::string>(root))
        {
            std::string root_name = std::get<std::string>(root);
            Node *root_node_ = nullptr;
            for (auto &node : s72_scene.nodes)
            {
                if (node.name == root_name)
                {
                    root_node_ = &node;
                }
            }
            assert(root_node_ != nullptr);

            std::vector<Node *> path;

            if (root_node_ != nullptr)
            {
                // Start DFS from this root node
                // s72_scene.nodes_map.push_back(root_node); // Store root node
                s72_scene.nodes_map[root_node_->name] = (root_node_);
                // std::cout << "\ndfs_build_tree\n";
                dfs_build_tree(root_node_, nullptr, path); // Build the tree from this root
            }
        }
    }
}

// set up initial node position/scale/rotation
void bind_driver()
{
    if (s72_scene.drivers.empty())
    {
        return;
    }

    for (auto &driver : s72_scene.drivers)
    {
        auto node_name = driver.refnode_name;
        if (s72_scene.nodes_map.find(node_name) != s72_scene.nodes_map.end())
        {
            Node *node_ = s72_scene.nodes_map[node_name];
            driver.position_init = node_->position;
            driver.rotation_init = node_->rotation;
            driver.scale_init = node_->scale;
        }
    }
}

void scene_workflow(sejp::value &val)
{
    // step0: clear vector
    s72_scene.nodes.clear();
    s72_scene.meshes.clear();
    s72_scene.materials.clear();
    s72_scene.cameras.clear();
    s72_scene.drivers.clear();
    s72_scene.textures_src.clear();
    s72_scene.lights.clear();

    // step1: load .s72, parse all information
    if (auto array_opt = val.as_array(); array_opt)
    {
        const auto &array = array_opt.value();
        get_scene(array);
    }

    // step1.5: add default lambertian material
    LambertianMaterial lamber = LambertianMaterial::LambertianMaterial();
    MaterialObject material_obj{
        .name = "default_lambertian",
        .type = LAMBERTIAN,
        .material = lamber,
    };
    s72_scene.materials.push_back(material_obj);

    // step2: build node trees and bind mesh, camera
    build_node_trees();

    // step3: bind driver to node
    bind_driver();

    // step4: add a camera if no camera exists
    if (s72_scene.cameras.empty())
    {
        std::cout << "\nmake user camera\n";
        make_user_camera();
    }

    // debug msg
    // print_s72();
}

void make_user_camera()
{
    Camera camera;

    camera.name = "User-Camera";
    camera.perspective.aspect = 1.77778f;
    camera.perspective.vfov = 2.08544f;
    camera.perspective.near = 0.1f;
    camera.perspective.far = 1000.f;

    s72_scene.cameras.push_back(camera);

    Node node;
    std::vector<Node *> path;
    Node *ref_node_ = find_node_by_name_or_index(s72_scene.scene.roots[0]);
    if (!ref_node_)
    {
        std::cerr << "ref_node_ is nullptr\n";
        return;
    }

    node.name = "User-Camera";
    node.position = 2.f * ref_node_->position;
    node.rotation = ref_node_->rotation;
    node.scale = ref_node_->scale;
    node.camera_name = "User-Camera";
    node.camera_ = &(s72_scene.cameras.back());

    s72_scene.nodes.push_back(node);
    s72_scene.nodes_map[node.name] = &(s72_scene.nodes.back());
    s72_scene.scene.roots.push_back(node.name);

    path.push_back(&node);
    s72_scene.cameras_path[node.camera_name] = path;

    // std::cout << "add user-camera done\n";
}

void setup_material_textureindex_map()
{
    // set default material with default texture
    std::vector<int> default_texture_index = {0, -1, -1};
    s72_scene.material_textureindex_map[&s72_scene.materials.back()] = default_texture_index;

    int index = 0;
    for (auto &material_obj : s72_scene.materials)
    {
        std::vector<int> texture_index(3, -1);

        if (std::holds_alternative<PBRMaterial>(material_obj.material))
        {
            PBRMaterial pbr = std::get<PBRMaterial>(material_obj.material);
            auto &albedo = pbr.albedo;
            if (std::holds_alternative<Texture>(albedo))
            {
                auto &texture = std::get<Texture>(albedo);
                if (s72_scene.textures_src_index_map.find(texture.src) != s72_scene.textures_src_index_map.end())
                {
                    auto albedo_index = s72_scene.textures_src_index_map[texture.src];
                    texture_index[0] = albedo_index;

                    s72_scene.material_textureindex_map[&material_obj] = texture_index;
                }
            }
        }
        else if (std::holds_alternative<LambertianMaterial>(material_obj.material))
        {
            LambertianMaterial lambertian = std::get<LambertianMaterial>(material_obj.material);
            auto &albedo = lambertian.albedo;
            if (std::holds_alternative<Texture>(albedo))
            {
                auto &texture = std::get<Texture>(albedo);
                if (s72_scene.textures_src_index_map.find(texture.src) != s72_scene.textures_src_index_map.end())
                {
                    auto albedo_index = s72_scene.textures_src_index_map[texture.src];
                    texture_index[0] = albedo_index;

                    s72_scene.material_textureindex_map[&material_obj] = texture_index;
                }
            }
        }

        if (material_obj.normalmap.has_value())
        {
            auto normalmap_name = material_obj.normalmap.value().src;
            if (s72_scene.textures_src_index_map.find(normalmap_name) != s72_scene.textures_src_index_map.end())
            {
                auto normalmap_index = s72_scene.textures_src_index_map[normalmap_name];
                texture_index[1] = normalmap_index;

                s72_scene.material_textureindex_map[&material_obj] = texture_index;
            }
        }
        if (material_obj.displacementmap.has_value())
        {
            auto displacementmap_name = material_obj.displacementmap.value().src;
            if (s72_scene.textures_src_index_map.find(displacementmap_name) != s72_scene.textures_src_index_map.end())
            {
                auto displacementmap_name_index = s72_scene.textures_src_index_map[displacementmap_name];
                texture_index[2] = displacementmap_name_index;

                s72_scene.material_textureindex_map[&material_obj] = texture_index;
            }
        }

        printf("material Index: %d, name: %s, texture( %d, %d, %d)\n", index, material_obj.name.c_str(), texture_index[0], texture_index[1], texture_index[2]);
        index++;
    }

    std::vector<int> t_index = s72_scene.material_textureindex_map[&s72_scene.materials.back()];
    printf("material name: %s, texture( %d, %d, %d)\n", s72_scene.materials.back().name.c_str(), t_index[0], t_index[1], t_index[2]);
    printf("texture size: %zd,  s72_scene.material_textureindex_map size : %zd\n", s72_scene.textures_src.size(), s72_scene.material_textureindex_map.size());
}

uint32_t convertToE5B9G9R9(float r, float g, float b)
{
    float maxRGB = std::max(r, std::max(g, b));
    int exponent = std::max(static_cast<int>(std::ceil(std::log2(maxRGB))), 0);
    float scale = std::pow(2.0f, exponent - 15.f); // Adjust to fit 5-bit exponent range

    // Scale RGB to fit 9-bit channels
    uint32_t R9 = static_cast<uint32_t>(std::min(511.0f, std::round(r / scale))) & 0x1FF;
    uint32_t G9 = static_cast<uint32_t>(std::min(511.0f, std::round(g / scale))) & 0x1FF;
    uint32_t B9 = static_cast<uint32_t>(std::min(511.0f, std::round(b / scale))) & 0x1FF;

    // Pack R9, G9, B9, and exponent into a 32-bit value
    uint32_t packed = (R9) | (G9 << 9) | (B9 << 18) | ((exponent & 0x1F) << 27);
    return packed;
}

glm::mat4 generate_transform(const Node *node)
{
    // Translation matrix
    glm::mat4 translation_matrix = glm::translate(glm::mat4(1.0f), node->position);

    // Rotation matrix from quaternion
    glm::mat4 rotation_matrix = glm::mat4_cast(node->rotation);

    // Scale matrix
    glm::mat4 scale_matrix = glm::scale(glm::mat4(1.0f), node->scale);

    // Combine translation, rotation, and scale to form the final WORLD_FROM_LOCAL matrix
    glm::mat4 WORLD_FROM_LOCAL = translation_matrix * rotation_matrix * scale_matrix;

    return WORLD_FROM_LOCAL;
}

void Driver::make_animation(float time)
{
    if (s72_scene.nodes_map.find(this->refnode_name) == s72_scene.nodes_map.end())
        return;

    Node *node_ = s72_scene.nodes_map[this->refnode_name];

    uint32_t size = (uint32_t)(this->frames.size());
    [[maybe_unused]] float min_time = this->frames[0].time,
                           max_time = this->frames[size - 1].time,
                           fraction;

    if (time > max_time)
    {
        current_frame = size - 1;
        next_frame = 0;
        fraction = (time - max_time) / (s72_scene.animation_duration - max_time);
        fraction = std::clamp(fraction, 0.f, 1.f);
    }
    else
    {
        while (time > this->frames[next_frame].time)
        {
            next_frame++;
            if (current_frame == size - 1) // a new loop
            {
                current_frame = 0;
            }
            else
            {
                current_frame++;
            }
        }
        fraction = (time - frames[current_frame].time) / (frames[next_frame].time - frames[current_frame].time);
        fraction = std::clamp(fraction, 0.f, 1.f);
    }

    // calculate interpolation matrix
    if (this->channel_dim == 3)
    {
        auto start = glm::vec3(frames[current_frame].value[0],
                               frames[current_frame].value[1],
                               frames[current_frame].value[2]);

        auto end = glm::vec3(frames[next_frame].value[0],
                             frames[next_frame].value[1],
                             frames[next_frame].value[2]);

        if (interpolation == STEP)
        {
            if (channel == TRANSLATION)
            {
                node_->position = start;
                // auto result = glm::translate(glm::mat4(1.0f), start);
                return;
            }
            else if (channel == SCALE)
            {
                node_->scale = start;
                return;
            }
        }
        else
        {
            auto vec3_interpolation = glm::mix(start, end, fraction);

            if (channel == TRANSLATION)
            {
                node_->position = vec3_interpolation;
                // return glm::translate(glm::mat4(1.0f), vec3_interpolation);
                return;
            }
            else if (channel == SCALE)
            {
                node_->scale = vec3_interpolation;
                // return glm::scale(glm::mat4(1.0f), vec3_interpolation);
                return;
            }
        }
    }
    else if (channel_dim == 4)
    {
        auto start = glm::quat(frames[current_frame].value[3],
                               frames[current_frame].value[0],
                               frames[current_frame].value[1],
                               frames[current_frame].value[2]);

        auto end = glm::quat(frames[next_frame].value[3],
                             frames[next_frame].value[0],
                             frames[next_frame].value[1],
                             frames[next_frame].value[2]);

        if (interpolation == SLERP)
        {
            glm::quat quat_interpolation = glm::slerp(start, end, fraction);
            node_->rotation = quat_interpolation;
            return; // glm::mat4_cast(quat_interpolation);
        }
        else
        {
            glm::quat quat_interpolation = glm::mix(start, end, fraction);
            node_->rotation = quat_interpolation;
            return; // glm::mat4_cast(quat_interpolation);
        }
    }
}

void Node::child_forward_kinematics_transforms(Node *node_)
{
    if (node_ == nullptr)
    {
        return;
    }

    for (const auto &child_ : node_->children_node_)
    {
        s72_scene.transforms[child_] = glm::mat4(child_->make_local_to_world());
        child_forward_kinematics_transforms(child_);
    }
}

//--------------------------------------
// the code below is from 15466 base code
// https://github.com/15-466/15-466-f24-base2
//--------------------------------------
glm::mat4x3 Node::make_local_to_parent() const
{
    // compute:
    //    translate   *   rotate    *   scale
    //  [ 1 0 0 p.x ]   [       0 ]   [ s.x 0 0 0 ]
    //  [ 0 1 0 p.y ] * [ rot   0 ] * [ 0 s.y 0 0 ]
    //  [ 0 0 1 p.z ]   [       0 ]   [ 0 0 s.z 0 ]
    //                  [ 0 0 0 1 ]   [ 0 0   0 1 ]
    glm::mat3 rot = glm::mat3_cast(rotation);

    return glm::mat4x3(
        rot[0] * scale.x, // scaling the columns here means that scale happens before rotation
        rot[1] * scale.y,
        rot[2] * scale.z,
        position);
}

glm::mat4x3 Node::make_parent_to_local() const
{
    // compute:
    //    1/scale       *    rot^-1   *  translate^-1
    //  [ 1/s.x 0 0 0 ]   [       0 ]   [ 0 0 0 -p.x ]
    //  [ 0 1/s.y 0 0 ] * [rot^-1 0 ] * [ 0 0 0 -p.y ]
    //  [ 0 0 1/s.z 0 ]   [       0 ]   [ 0 0 0 -p.z ]
    //                    [ 0 0 0 1 ]   [ 0 0 0  1   ]
    glm::vec3 inv_scale;
    // taking some care so that we don't end up with NaN's , just a degenerate matrix, if scale is zero:
    inv_scale.x = (scale.x == 0.0f ? 0.0f : 1.0f / scale.x);
    inv_scale.y = (scale.y == 0.0f ? 0.0f : 1.0f / scale.y);
    inv_scale.z = (scale.z == 0.0f ? 0.0f : 1.0f / scale.z);

    // compute inverse of rotation:
    glm::mat3 inv_rot = glm::mat3_cast(glm::inverse(rotation));

    // scale the rows of rot:
    inv_rot[0] *= inv_scale;
    inv_rot[1] *= inv_scale;
    inv_rot[2] *= inv_scale;

    return glm::mat4x3(
        inv_rot[0],
        inv_rot[1],
        inv_rot[2],
        inv_rot * -position);
}

glm::mat4x3 Node::make_local_to_world() const
{
    if (!parent_)
    {
        return make_local_to_parent();
    }
    else
    {
        return parent_->make_local_to_world() * glm::mat4(make_local_to_parent()); // note: glm::mat4(glm::mat4x3) pads with a (0,0,0,1) row
    }
}
glm::mat4x3 Node::make_world_to_local() const
{
    if (!parent_)
    {
        return make_parent_to_local();
    }
    else
    {
        return make_parent_to_local() * glm::mat4(parent_->make_world_to_local()); // note: glm::mat4(glm::mat4x3) pads with a (0,0,0,1) row
    }
}

glm::quat extract_rotation_quaternion(glm::mat4 &localToWorld)
{
    return glm::quat_cast(glm::mat3(localToWorld));
}

glm::mat4 Camera::make_projection() const
{
    return glm::perspective(perspective.vfov, perspective.aspect, perspective.near, perspective.far);
}

void print_s72()
{
    std::cout << "\nscene name: " << s72_scene.scene.name << std::endl;
    printf("roots.size(): %zd, nodes.size(): %zd, meshes.size(): %zd, materials.size(): %zd\n", s72_scene.scene.roots.size(), s72_scene.nodes.size(),
           s72_scene.meshes.size(), s72_scene.materials.size());

    for (auto &a : s72_scene.scene.roots)
    {
        std::visit([](const auto &value)
                   {
                       std::cout << value; // Print the string or double directly
                   },
                   a);
        std::cout << ", ";
    }
    std::cout << "\n";

    // Print all Node information
    std::cout << "Nodes information: " << std::endl;
    for (const auto &node : s72_scene.nodes)
    {
        // Print Node's name
        std::cout << "Node name: " << node.name << std::endl;

        // Print Translation
        // std::cout << "  Translation: ["
        //           << node.position.x << ", "
        //           << node.position.y << ", "
        //           << node.position.z << "]" << std::endl;

        // Print Children
        if (!node.children.empty())
        {
            std::cout << "  Children: ";
            for (auto &a : node.children)
            {
                std::visit([](const auto &value)
                           {
                               std::cout << value; // Print the string or double directly
                           },
                           a);
                std::cout << ", ";
            }
            std::cout << std::endl;
        }

        // Print optional fields
        if (!node.mesh_name.empty())
        {
            std::cout << "  Mesh: " << node.mesh_name << std::endl;
        }
    }

    std::cout << "Meshes count: " << s72_scene.meshes.size() << "\n";
    std::cout << "Print mesh_material_map size: " << s72_scene.mesh_material_map.size() << "\n";

    for (const auto &[mesh_, material_] : s72_scene.mesh_material_map)
    {
        std::cout << mesh_->name << " : " << material_->name << "  type: " << material_->type << std::endl;
    }

    for (auto &light : s72_scene.lights)
    {
        printf("%s: %d, %d, tint: %f, %f, %f.\n", light.name.c_str(), light.shadow, light.type,
               light.tint.x, light.tint.y, light.tint.z);

        Node *node_ = s72_scene.light_node_map[&light].front();
        auto transform = s72_scene.transforms[node_];

        printf("%f %f %f %f\n%f %f %f %f\n%f %f %f %f\n%f %f %f %f\n",
               transform[0][0], transform[0][1], transform[0][2], transform[0][3],
               transform[1][0], transform[1][1], transform[1][2], transform[1][3],
               transform[2][0], transform[2][1], transform[2][2], transform[2][3],
               transform[3][0], transform[3][1], transform[3][2], transform[3][3]);
        printf("position: %f %f %f %f\n", transform[3][0], transform[3][1], transform[3][2], transform[3][3]);
        if (light.type == SUN)
        {
            printf("\"sun:\" angle: %f, strength: %f\n", light.data.sun.angle, light.data.sun.strength);
        }
        else if (light.type == SPHERE)
        {
            printf("\"sphere:\" radius: %f, power: %f, limit: %f\n", light.data.sphere.radius, light.data.sphere.power, light.data.sphere.limit);
        }
        else if (light.type == SPOT)
        {
            printf("\"spot:\" radius: %f, power: %f, fov: %f, blend: %f, limit: %f\n",
                   light.data.spot.radius, light.data.spot.power, light.data.spot.fov, light.data.spot.blend, light.data.spot.limit);
        }
    }

    // std::cin.get();
    /*
        if (!s72_scene.materials.empty())
        {
            std::cout << "\nmaterial object[0] name: " << s72_scene.materials[0].name << "  ";
            std::cout << "normalMap: ";
            if (s72_scene.materials[0].normalmap.has_value())
            {
                std::cout << s72_scene.materials[0].normalmap.value().src;
            };
            std::cout << "  type: " << s72_scene.materials[0].type << "  ";
            if (s72_scene.materials[0].normalmap.has_value())
            {
                std::cout << s72_scene.materials[0].normalmap.value().src;
            };
            if (std::holds_alternative<PBRMaterial>(s72_scene.materials[0].material))
            {
                // The variant holds a PBRMaterial
                // PBRMaterial &pbr = std::get<PBRMaterial>(material);
                // Use 'pbr'
                std::cout << "pbr\n";
            }
            else if (std::holds_alternative<LambertianMaterial>(s72_scene.materials[0].material))
            {
                // The variant holds a LambertianMaterial
                // LambertianMaterial &lambertian = std::get<LambertianMaterial>(material);
                // Use 'lambertian'
                std::cout << "lambertian\n";
            }
        }
        else
        {
            std::cout << "\nno material!!!!\n";
        }
        */
}
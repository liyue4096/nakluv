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
                }

                // Get "normalMap" field
                if (auto normalmap_opt = obj.find("normalMap"); normalmap_opt != obj.end() && normalmap_opt->second.as_object())
                {
                    auto &normalmap_obj = normalmap_opt->second.as_object().value();

                    if (auto src_opt = normalmap_obj.find("src"); src_opt != normalmap_obj.end() && src_opt->second.as_string())
                    {
                        material.normalmap = Texture{src_opt->second.as_string().value()};
                    }
                }

                // Get "displacementMap" field
                if (auto displacementmap_opt = obj.find("displacementMap"); displacementmap_opt != obj.end() && displacementmap_opt->second.as_object())
                {
                    auto &isplacementmap_obj = displacementmap_opt->second.as_object().value();
                    if (auto src_opt = isplacementmap_obj.find("src"); src_opt != isplacementmap_obj.end() && src_opt->second.as_string())
                    {
                        material.displacementmap = Texture{src_opt->second.as_string().value()};
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
                Environment env;
                // Get "name" field
                if (auto name_opt = obj.find("name"); name_opt != obj.end() && name_opt->second.as_string())
                {
                    env.name = name_opt->second.as_string().value();
                }
                // Get "radiance" field
                if (auto name_opt = obj.find("radiance"); name_opt != obj.end() && name_opt->second.as_string())
                {
                    std::string channel_str = name_opt->second.as_string().value();
                    if (channel_str == "src")
                    {
                        env.radiance.src = name_opt->second.as_string().value();
                    }
                    if (channel_str == "type")
                    {
                        env.radiance.type = name_opt->second.as_string().value();
                    }
                    if (channel_str == "format")
                    {
                        env.radiance.format = name_opt->second.as_string().value();
                    }
                }

                s72_scene.environment = env;
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
        if (node_index >= 0 && node_index < static_cast<int>(s72_scene.nodes.size()))
        {
            return &s72_scene.nodes[node_index];
        }
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
        Node *root_node = find_node_by_name_or_index(root);
        std::vector<Node *> path;

        if (root_node != nullptr)
        {
            // Start DFS from this root node
            // s72_scene.nodes_map.push_back(root_node); // Store root node
            s72_scene.nodes_map[root_node->name] = (root_node);
            // std::cout << "\ndfs_build_tree\n";
            dfs_build_tree(root_node, nullptr, path); // Build the tree from this root
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
    // step1: load .s72, parse all information
    if (auto array_opt = val.as_array(); array_opt)
    {
        const auto &array = array_opt.value();
        get_scene(array);
    }

    // debug: print msg
    if (0)
    {
        std::cout << "\nscene name: " << s72_scene.scene.name << std::endl;
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
            std::cout << "  Translation: ["
                      << node.position.x << ", "
                      << node.position.y << ", "
                      << node.position.z << "]" << std::endl;

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
    }

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

glm::mat4 Camera::make_projection() const
{
    return glm::perspective(perspective.vfov, perspective.aspect, perspective.near, perspective.far);
}
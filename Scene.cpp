#include "Scene.hpp"

// Define the global variable
S72_scene s72_scene;

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
            if (type_opt->second.as_string().value() == "NODE")
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
                        node.Translation.tx = (float)translation_array[0].as_number().value_or(0.f);
                        node.Translation.ty = (float)translation_array[1].as_number().value_or(0.f);
                        node.Translation.tz = (float)translation_array[2].as_number().value_or(0.f);
                    }
                }

                // Get "rotation" field
                if (auto rotation_opt = obj.find("rotation"); rotation_opt != obj.end() && rotation_opt->second.as_array())
                {
                    const auto &rotation_array = rotation_opt->second.as_array().value();
                    if (rotation_array.size() == 4)
                    {
                        node.Rotation.rx = (float)rotation_array[0].as_number().value_or(0.f);
                        node.Rotation.ry = (float)rotation_array[1].as_number().value_or(0.f);
                        node.Rotation.rz = (float)rotation_array[2].as_number().value_or(0.f);
                        node.Rotation.rw = (float)rotation_array[3].as_number().value_or(1.f);
                    }
                }

                // Get "scale" field
                if (auto scale_opt = obj.find("scale"); scale_opt != obj.end() && scale_opt->second.as_array())
                {
                    const auto &scale_array = scale_opt->second.as_array().value();
                    if (scale_array.size() == 3)
                    {
                        node.Scale.sx = (float)scale_array[0].as_number().value_or(1.f);
                        node.Scale.sy = (float)scale_array[1].as_number().value_or(1.f);
                        node.Scale.sz = (float)scale_array[2].as_number().value_or(1.f);
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
            if (type_opt->second.as_string().value() == "MESH")
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
            if (type_opt->second.as_string().value() == "CAMERA")
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
void dfs_build_tree(Node *current_node)
{
    if (!current_node)
        return;

    // Bind the mesh if the node has a valid mesh_name
    if (!current_node->mesh_name.empty())
    {
        current_node->mesh_ = find_mesh_by_name(current_node->mesh_name);
    }

    // Bind the camera if the node has a valid camera_name
    if (!current_node->camera_name.empty())
    {
        current_node->camera_ = find_camera_by_name(current_node->camera_name);
    }

    // Process each child of the current node
    for (const auto &child : current_node->children)
    {
        Node *child_node = find_node_by_name_or_index(child);
        if (child_node)
        {
            std::cout << " child: " << child_node->name << "  mesh: " << child_node->mesh_name << " to parent: " << current_node->name << "\n";
            //  Recursively build the tree for the child
            // s72_scene.roots.push_back(child_node); // Store in roots
            s72_scene.roots[child_node->name] = (child_node);
            dfs_build_tree(child_node); // Continue DFS
        }
    }
}

void build_node_trees()
{
    s72_scene.roots.clear();

    for (auto &root : s72_scene.scene.roots)
    {
        Node *root_node = find_node_by_name_or_index(root);
        if (root_node != nullptr)
        {
            // Start DFS from this root node
            // s72_scene.roots.push_back(root_node); // Store root node
            s72_scene.roots[root_node->name] = (root_node);
            dfs_build_tree(root_node); // Build the tree from this root
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
    if (1)
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
                      << node.Translation.tx << ", "
                      << node.Translation.ty << ", "
                      << node.Translation.tz << "]" << std::endl;

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

    // step2: build node trees and bind mesh, camera
    build_node_trees();
}
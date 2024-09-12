#ifdef _WIN32
// ensure we have M_PI
#define _USE_MATH_DEFINES
#endif

#include "Tutorial.hpp"
#include "refsol.hpp"

#include "VK.hpp"
#include "refsol.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

Tutorial::Tutorial(RTG &rtg_) : rtg(rtg_)
{
	refsol::Tutorial_constructor(rtg, &depth_format, &render_pass, &command_pool);

	background_pipeline.create(rtg, render_pass, 0);
	lines_pipeline.create(rtg, render_pass, 0);
	objects_pipeline.create(rtg, render_pass, 0);

	{															  // create descriptor pool:
		uint32_t per_workspace = uint32_t(rtg.workspaces.size()); // for easier-to-read counting

		std::array<VkDescriptorPoolSize, 2> pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 2 * per_workspace, // one descriptor per set, one set per workspace
			},
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1 * per_workspace, // one descriptor per set, one set per workspace
			},
		};

		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0,					  // because CREATE_FREE_DESCRIPTOR_SET_BIT isn't included, *can't* free individual descriptors allocated from this pool
			.maxSets = 3 * per_workspace, // three set per workspace
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool));
	}

	workspaces.resize(rtg.workspaces.size());

	for (Workspace &workspace : workspaces)
	{
		refsol::Tutorial_constructor_workspace(rtg, command_pool, &workspace.command_buffer);

		workspace.Camera_src = rtg.helpers.create_buffer(
			sizeof(LinesPipeline::Camera),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,											// going to have GPU copy from this memory
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // host-visible memory, coherent (no special sync needed)
			Helpers::Mapped																// get a pointer to the memory
		);
		workspace.Camera = rtg.helpers.create_buffer(
			sizeof(LinesPipeline::Camera),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, // going to use as a uniform buffer, also going to have GPU copy into this memory
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,								   // GPU-local memory
			Helpers::Unmapped													   // don't get a pointer to the memory
		);

		{ // allocate descriptor set for Camera descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &lines_pipeline.set0_Camera,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Camera_descriptors));
		}

		workspace.World_src = rtg.helpers.create_buffer(
			sizeof(ObjectsPipeline::World),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			Helpers::Mapped);
		workspace.World = rtg.helpers.create_buffer(
			sizeof(ObjectsPipeline::World),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped);

		{ // allocate descriptor set for World descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &objects_pipeline.set0_World,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.World_descriptors));
			// NOTE: will actually fill in this descriptor set just a bit lower
		}

		{ // allocate descriptor set for Transforms descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &objects_pipeline.set1_Transforms,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Transforms_descriptors));
			// NOTE: will fill in this descriptor set in render when buffers are [re-]allocated
		}

		{ // point descriptor to Camera buffer:
			VkDescriptorBufferInfo Camera_info{
				.buffer = workspace.Camera.handle,
				.offset = 0,
				.range = workspace.Camera.size,
			};

			VkDescriptorBufferInfo World_info{
				.buffer = workspace.World.handle,
				.offset = 0,
				.range = workspace.World.size,
			};

			std::array<VkWriteDescriptorSet, 2> writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Camera_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &Camera_info,
				},
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.World_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &World_info,
				},
			};

			vkUpdateDescriptorSets(
				rtg.device,				 // device
				uint32_t(writes.size()), // descriptorWriteCount
				writes.data(),			 // pDescriptorWrites
				0,						 // descriptorCopyCount
				nullptr					 // pDescriptorCopies
			);
		}
	}

	{ // create object vertices
		std::vector<PosNorTexVertex> vertices;

		// { // A [-1,1]x[-1,1]x{0} quadrilateral:
		// 	plane_vertices.first = uint32_t(vertices.size());
		// 	vertices.emplace_back(PosNorTexVertex{
		// 		.Position{.x = -1.0f, .y = -1.0f, .z = 0.0f},
		// 		.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
		// 		.TexCoord{.s = 0.0f, .t = 0.0f},
		// 	});
		// 	vertices.emplace_back(PosNorTexVertex{
		// 		.Position{.x = 1.0f, .y = -1.0f, .z = 0.0f},
		// 		.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
		// 		.TexCoord{.s = 1.0f, .t = 0.0f},
		// 	});
		// 	vertices.emplace_back(PosNorTexVertex{
		// 		.Position{.x = -1.0f, .y = 1.0f, .z = 0.0f},
		// 		.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
		// 		.TexCoord{.s = 0.0f, .t = 1.0f},
		// 	});
		// 	vertices.emplace_back(PosNorTexVertex{
		// 		.Position{.x = 1.0f, .y = 1.0f, .z = 0.0f},
		// 		.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
		// 		.TexCoord{.s = 1.0f, .t = 1.0f},
		// 	});
		// 	vertices.emplace_back(PosNorTexVertex{
		// 		.Position{.x = -1.0f, .y = 1.0f, .z = 0.0f},
		// 		.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
		// 		.TexCoord{.s = 0.0f, .t = 1.0f},
		// 	});
		// 	vertices.emplace_back(PosNorTexVertex{
		// 		.Position{.x = 1.0f, .y = -1.0f, .z = 0.0f},
		// 		.Normal{.x = 0.0f, .y = 0.0f, .z = 1.0f},
		// 		.TexCoord{.s = 1.0f, .t = 0.0f},
		// 	});

		// 	plane_vertices.count = uint32_t(vertices.size()) - plane_vertices.first;
		// }

		{ // A torus:
			torus_vertices.first = uint32_t(vertices.size());

			// will parameterize with (u,v) where:
			//  - u is angle around main axis (+z)
			//  - v is angle around the tube

			constexpr float R1 = 0.7f;	// main radius
			constexpr float R2 = 0.05f; // tube radius

			constexpr uint32_t U_STEPS = 50;
			constexpr uint32_t V_STEPS = 40;

			// texture repeats around the torus:
			constexpr float V_REPEATS = 1.0f;
			float U_REPEATS = std::ceil(V_REPEATS / R2 * R1);

			auto emplace_vertex = [&](uint32_t ui, uint32_t vi)
			{
				// convert steps to angles:
				//  (doing the mod since trig on 2 M_PI may not exactly match 0)
				float ua = (ui % U_STEPS) / float(U_STEPS) * 2.0f * float(M_PI);
				float va = (vi % V_STEPS) / float(V_STEPS) * 2.0f * float(M_PI);

				vertices.emplace_back(PosNorTexVertex{
					.Position{
						.x = (R1 + R2 * std::cos(va)) * std::cos(ua),
						.y = (R1 + R2 * std::cos(va)) * std::sin(ua),
						.z = R2 * std::sin(va),
					},
					.Normal{
						.x = std::cos(va) * std::cos(ua),
						.y = std::cos(va) * std::sin(ua),
						.z = std::sin(va),
					},
					.TexCoord{
						.s = ui / float(U_STEPS) * U_REPEATS,
						.t = vi / float(V_STEPS) * V_REPEATS,
					},
				});
			};

			for (uint32_t ui = 0; ui < U_STEPS; ++ui)
			{
				for (uint32_t vi = 0; vi < V_STEPS; ++vi)
				{
					emplace_vertex(ui, vi);
					emplace_vertex(ui + 1, vi);
					emplace_vertex(ui, vi + 1);

					emplace_vertex(ui, vi + 1);
					emplace_vertex(ui + 1, vi);
					emplace_vertex(ui + 1, vi + 1);
				}
			}

			torus_vertices.count = uint32_t(vertices.size()) - torus_vertices.first;
		}

		{ // A smaller torus rotating inside the larger one:
			torus_vertices_1.first = uint32_t(vertices.size());

			constexpr float R1_small = 0.6f;  // main radius (smaller torus)
			constexpr float R2_small = 0.05f; // tube radius (smaller torus)

			constexpr uint32_t U_STEPS = 50;
			constexpr uint32_t V_STEPS = 40;

			constexpr float V_REPEATS = 1.0f;
			float U_REPEATS = std::ceil(V_REPEATS / R2_small * R1_small);

			float rotation_angle = 0.0f; // Initial rotation angle around the x-axis

			auto emplace_vertex = [&](uint32_t ui, uint32_t vi, float r1, float r2, float rotation_angle)
			{
				float ua = (ui % U_STEPS) / float(U_STEPS) * 2.0f * float(M_PI);
				float va = (vi % V_STEPS) / float(V_STEPS) * 2.0f * float(M_PI);

				// Torus position before any transformation
				float z = (r1 + r2 * std::cos(va)) * std::cos(ua);
				float y = (r1 + r2 * std::cos(va)) * std::sin(ua);
				float x = r2 * std::sin(va);

				// Apply rotation around the x-axis for the smaller torus
				float new_y = y * std::cos(rotation_angle) - z * std::sin(rotation_angle);
				float new_z = y * std::sin(rotation_angle) + z * std::cos(rotation_angle);
				y = new_y;
				z = new_z;

				vertices.emplace_back(PosNorTexVertex{
					.Position{.x = x, .y = y, .z = z},
					.Normal{
						.x = std::cos(va) * std::cos(ua),
						.y = std::cos(va) * std::sin(ua),
						.z = std::sin(va),
					},
					.TexCoord{
						.s = ui / float(U_STEPS) * U_REPEATS,
						.t = vi / float(V_STEPS) * V_REPEATS,
					},
				});
			};

			// Generate the smaller torus vertices with rotation
			for (uint32_t ui = 0; ui < U_STEPS; ++ui)
			{
				for (uint32_t vi = 0; vi < V_STEPS; ++vi)
				{
					emplace_vertex(ui, vi, R1_small, R2_small, rotation_angle); // For smaller torus
					emplace_vertex(ui + 1, vi, R1_small, R2_small, rotation_angle);
					emplace_vertex(ui, vi + 1, R1_small, R2_small, rotation_angle);

					emplace_vertex(ui, vi + 1, R1_small, R2_small, rotation_angle);
					emplace_vertex(ui + 1, vi, R1_small, R2_small, rotation_angle);
					emplace_vertex(ui + 1, vi + 1, R1_small, R2_small, rotation_angle);
				}
			}

			torus_vertices_1.count = uint32_t(vertices.size()) - torus_vertices_1.first;
		}

		{ // A smaller torus rotating inside the larger one:
			torus_vertices_2.first = uint32_t(vertices.size());

			constexpr float R1_small = 0.5f;  // main radius (smaller torus)
			constexpr float R2_small = 0.05f; // tube radius (smaller torus)

			constexpr uint32_t U_STEPS = 50;
			constexpr uint32_t V_STEPS = 40;

			constexpr float V_REPEATS = 1.0f;
			float U_REPEATS = std::ceil(V_REPEATS / R2_small * R1_small);

			float rotation_angle = 0.0f; // Initial rotation angle around the x-axis

			auto emplace_vertex = [&](uint32_t ui, uint32_t vi, float r1, float r2, float rotation_angle)
			{
				float ua = (ui % U_STEPS) / float(U_STEPS) * 2.0f * float(M_PI);
				float va = (vi % V_STEPS) / float(V_STEPS) * 2.0f * float(M_PI);

				// Torus position before any transformation
				float x = (r1 + r2 * std::cos(va)) * std::cos(ua);
				float z = (r1 + r2 * std::cos(va)) * std::sin(ua);
				float y = r2 * std::sin(va);

				// Apply rotation around the x-axis for the smaller torus
				float new_y = y * std::cos(rotation_angle) - z * std::sin(rotation_angle);
				float new_z = y * std::sin(rotation_angle) + z * std::cos(rotation_angle);
				y = new_y;
				z = new_z;

				vertices.emplace_back(PosNorTexVertex{
					.Position{.x = x, .y = y, .z = z},
					.Normal{
						.x = std::cos(va) * std::cos(ua),
						.y = std::cos(va) * std::sin(ua),
						.z = std::sin(va),
					},
					.TexCoord{
						.s = ui / float(U_STEPS) * U_REPEATS,
						.t = vi / float(V_STEPS) * V_REPEATS,
					},
				});
			};

			// Generate the smaller torus vertices with rotation
			for (uint32_t ui = 0; ui < U_STEPS; ++ui)
			{
				for (uint32_t vi = 0; vi < V_STEPS; ++vi)
				{
					emplace_vertex(ui, vi, R1_small, R2_small, rotation_angle); // For smaller torus
					emplace_vertex(ui + 1, vi, R1_small, R2_small, rotation_angle);
					emplace_vertex(ui, vi + 1, R1_small, R2_small, rotation_angle);

					emplace_vertex(ui, vi + 1, R1_small, R2_small, rotation_angle);
					emplace_vertex(ui + 1, vi, R1_small, R2_small, rotation_angle);
					emplace_vertex(ui + 1, vi + 1, R1_small, R2_small, rotation_angle);
				}
			}

			torus_vertices_2.count = uint32_t(vertices.size()) - torus_vertices_2.first;
		}

		{ // A smaller torus rotating inside the larger one:
			torus_vertices_3.first = uint32_t(vertices.size());

			constexpr float R1_small = 0.4f;  // main radius (smaller torus)
			constexpr float R2_small = 0.05f; // tube radius (smaller torus)

			constexpr uint32_t U_STEPS = 50;
			constexpr uint32_t V_STEPS = 40;

			constexpr float V_REPEATS = 1.0f;
			float U_REPEATS = std::ceil(V_REPEATS / R2_small * R1_small);

			float rotation_angle = 0.0f; // Initial rotation angle around the x-axis

			auto emplace_vertex = [&](uint32_t ui, uint32_t vi, float r1, float r2, float rotation_angle)
			{
				float ua = (ui % U_STEPS) / float(U_STEPS) * 2.0f * float(M_PI);
				float va = (vi % V_STEPS) / float(V_STEPS) * 2.0f * float(M_PI);

				// Torus position before any transformation
				float x = (r1 + r2 * std::cos(va)) * std::cos(ua);
				float y = (r1 + r2 * std::cos(va)) * std::sin(ua);
				float z = r2 * std::sin(va);

				// Apply rotation around the x-axis for the smaller torus
				float new_y = y * std::cos(rotation_angle) - z * std::sin(rotation_angle);
				float new_z = y * std::sin(rotation_angle) + z * std::cos(rotation_angle);
				y = new_y;
				z = new_z;

				vertices.emplace_back(PosNorTexVertex{
					.Position{.x = x, .y = y, .z = z},
					.Normal{
						.x = std::cos(va) * std::cos(ua),
						.y = std::cos(va) * std::sin(ua),
						.z = std::sin(va),
					},
					.TexCoord{
						.s = ui / float(U_STEPS) * U_REPEATS,
						.t = vi / float(V_STEPS) * V_REPEATS,
					},
				});
			};

			// Generate the smaller torus vertices with rotation
			for (uint32_t ui = 0; ui < U_STEPS; ++ui)
			{
				for (uint32_t vi = 0; vi < V_STEPS; ++vi)
				{
					emplace_vertex(ui, vi, R1_small, R2_small, rotation_angle); // For smaller torus
					emplace_vertex(ui + 1, vi, R1_small, R2_small, rotation_angle);
					emplace_vertex(ui, vi + 1, R1_small, R2_small, rotation_angle);

					emplace_vertex(ui, vi + 1, R1_small, R2_small, rotation_angle);
					emplace_vertex(ui + 1, vi, R1_small, R2_small, rotation_angle);
					emplace_vertex(ui + 1, vi + 1, R1_small, R2_small, rotation_angle);
				}
			}

			torus_vertices_3.count = uint32_t(vertices.size()) - torus_vertices_3.first;
		}

		{ // Add a rotating triangular prism inside the smaller torus
			tetrahedron.first = uint32_t(vertices.size());
			constexpr float tetrahedron_size = 0.2f;							  // The size/scale of the tetrahedron
			float rotation_angle_tetrahedron = time / 60.0f * 2.0f * float(M_PI); // Time-based rotation for animation

			// Define a 3D point structure
			struct Vec3
			{
				float x, y, z;
			};

			// Vertices of a regular tetrahedron
			std::array<Vec3, 4> vertices_tetrahedron = {
				// These are normalized coordinates for a regular tetrahedron centered at the origin
				Vec3{tetrahedron_size * 1.0f, tetrahedron_size * 1.0f, tetrahedron_size * 1.0f},
				Vec3{tetrahedron_size * -1.0f, tetrahedron_size * -1.0f, tetrahedron_size * 1.0f},
				Vec3{tetrahedron_size * -1.0f, tetrahedron_size * 1.0f, tetrahedron_size * -1.0f},
				Vec3{tetrahedron_size * 1.0f, tetrahedron_size * -1.0f, tetrahedron_size * -1.0f}};

			// Function to rotate a point around the Z-axis
			auto rotate_vertex_around_z = [&](Vec3 vertex, float angle)
			{
				float new_x = vertex.x * std::cos(angle) - vertex.y * std::sin(angle);
				float new_y = vertex.x * std::sin(angle) + vertex.y * std::cos(angle);
				return Vec3{new_x, new_y, vertex.z};
			};

			// Add tetrahedron faces (each face is a triangle) using PosNorTexVertex format
			// Define the faces of the tetrahedron (4 triangles)
			std::array<std::array<int, 3>, 4> faces = {{
				{0, 1, 2}, // First face
				{0, 1, 3}, // Second face
				{0, 2, 3}, // Third face
				{1, 2, 3}  // Fourth face
			}};

			// Add the faces of the tetrahedron
			for (const auto &face : faces)
			{
				// Rotate each vertex in the face
				Vec3 rotated_vertex_1 = rotate_vertex_around_z(vertices_tetrahedron[face[0]], rotation_angle_tetrahedron);
				Vec3 rotated_vertex_2 = rotate_vertex_around_z(vertices_tetrahedron[face[1]], rotation_angle_tetrahedron);
				Vec3 rotated_vertex_3 = rotate_vertex_around_z(vertices_tetrahedron[face[2]], rotation_angle_tetrahedron);

				// Compute normal for this triangle (cross product of two edges)
				Vec3 edge1{rotated_vertex_2.x - rotated_vertex_1.x, rotated_vertex_2.y - rotated_vertex_1.y, rotated_vertex_2.z - rotated_vertex_1.z};
				Vec3 edge2{rotated_vertex_3.x - rotated_vertex_1.x, rotated_vertex_3.y - rotated_vertex_1.y, rotated_vertex_3.z - rotated_vertex_1.z};
				Vec3 normal{
					edge1.y * edge2.z - edge1.z * edge2.y,
					edge1.z * edge2.x - edge1.x * edge2.z,
					edge1.x * edge2.y - edge1.y * edge2.x};

				// Add the three vertices for the triangle face with computed normal
				vertices.emplace_back(PosNorTexVertex{
					.Position{.x = rotated_vertex_1.x, .y = rotated_vertex_1.y, .z = rotated_vertex_1.z},
					.Normal{.x = normal.x, .y = normal.y, .z = normal.z},
					.TexCoord{.s = 0.0f, .t = 0.0f}});
				vertices.emplace_back(PosNorTexVertex{
					.Position{.x = rotated_vertex_2.x, .y = rotated_vertex_2.y, .z = rotated_vertex_2.z},
					.Normal{.x = normal.x, .y = normal.y, .z = normal.z},
					.TexCoord{.s = 0.0f, .t = 0.0f}});
				vertices.emplace_back(PosNorTexVertex{
					.Position{.x = rotated_vertex_3.x, .y = rotated_vertex_3.y, .z = rotated_vertex_3.z},
					.Normal{.x = normal.x, .y = normal.y, .z = normal.z},
					.TexCoord{.s = 0.0f, .t = 0.0f}});
			}
			tetrahedron.count = uint32_t(vertices.size()) - tetrahedron.first;
		}

		size_t bytes = vertices.size() * sizeof(vertices[0]);

		object_vertices = rtg.helpers.create_buffer(
			bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped);

		// copy data to buffer:
		rtg.helpers.transfer_to_buffer(vertices.data(), bytes, object_vertices);
	}

	{ // make some textures
		textures.reserve(2);

		{ // texture 0 will be a dark grey / light grey checkerboard with a red square at the origin.
			// actually make the texture:
			uint32_t size = 128;
			std::vector<uint32_t> data;
			data.reserve(size * size);
			for (uint32_t y = 0; y < size; ++y)
			{
				float fy = (y + 0.5f) / float(size);
				for (uint32_t x = 0; x < size; ++x)
				{
					float fx = (x + 0.5f) / float(size);
					// highlight the origin:
					if (fx < 0.05f && fy < 0.05f)
						data.emplace_back(0xff0000ff); // red
					else if ((fx < 0.5f) == (fy < 0.5f))
						data.emplace_back(0xff444444); // dark grey
					else
						data.emplace_back(0xffbbbbbb); // light grey
				}
			}
			assert(data.size() == size * size);

			// make a place for the texture to live on the GPU:
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{.width = size, .height = size}, // size of image
				VK_FORMAT_R8G8B8A8_UNORM,				   // how to interpret image data (in this case, linearly-encoded 8-bit RGBA)
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // should be device-local
				Helpers::Unmapped));

			// transfer data:
			rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
		}

		{ // texture 1 will be a classic 'xor' texture:
			// actually make the texture:
			uint32_t size = 256;
			std::vector<uint32_t> data;
			data.reserve(size * size);
			for (uint32_t y = 0; y < size; ++y)
			{
				for (uint32_t x = 0; x < size; ++x)
				{
					uint8_t r = uint8_t(x) ^ uint8_t(y);
					uint8_t g = uint8_t(x + 128) ^ uint8_t(y);
					uint8_t b = uint8_t(x) ^ uint8_t(y + 27);
					uint8_t a = 0xff;
					data.emplace_back(uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24));
				}
			}
			assert(data.size() == size * size);

			// make a place for the texture to live on the GPU:
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{.width = size, .height = size}, // size of image
				VK_FORMAT_R8G8B8A8_SRGB,				   // how to interpret image data (in this case, SRGB-encoded 8-bit RGBA)
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // should be device-local
				Helpers::Unmapped));

			// transfer data:
			rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
		}
	}

	{ // texture 2 will be a metallic bronze texture:
		// set the size of the texture:
		uint32_t size = 256;
		std::vector<uint32_t> data;
		data.reserve(size * size);

		// iterate over each pixel:
		for (uint32_t y = 0; y < size; ++y)
		{
			for (uint32_t x = 0; x < size; ++x)
			{
				// Create a metallic bronze color by adjusting the r, g, b channels
				// Introduce some shininess by modulating the colors based on x and y
				uint8_t base_r = 150 + uint8_t((std::sin(x * 0.1f) + std::sin(y * 0.1f)) * 30.0f);	// base red with light variation
				uint8_t base_g = 60 + uint8_t((std::sin(x * 0.15f) + std::sin(y * 0.15f)) * 25.0f); // base green
				uint8_t base_b = 30 + uint8_t((std::cos(x * 0.1f) + std::cos(y * 0.1f)) * 10.0f);	// base blue, darker for bronze

				// Apply metallic shininess by adding highlights:
				float shine = std::abs(std::sin(x * 0.05f + y * 0.05f)) * 50.0f;

				// Combine the base color with the shininess:
				uint8_t r = uint8_t(std::min(255.0f, base_r + shine));
				uint8_t g = uint8_t(std::min(255.0f, base_g + shine));
				uint8_t b = uint8_t(std::min(255.0f, base_b + shine));
				uint8_t a = 0xff; // fully opaque

				// Combine r, g, b, a into a single 32-bit value:
				data.emplace_back(uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24));
			}
		}

		assert(data.size() == size * size);

		// create the image on the GPU:
		textures.emplace_back(rtg.helpers.create_image(
			VkExtent2D{.width = size, .height = size}, // size of image
			VK_FORMAT_R8G8B8A8_SRGB,				   // 8-bit RGBA with SRGB encoding
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // sample and upload
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // device-local memory
			Helpers::Unmapped));

		// transfer the generated data to the GPU:
		rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
	}

	{ // texture 3 will be a metallic gold texture:
		// set the size of the texture:
		uint32_t size = 256;
		std::vector<uint32_t> data;
		data.reserve(size * size);

		// iterate over each pixel:
		for (uint32_t y = 0; y < size; ++y)
		{
			for (uint32_t x = 0; x < size; ++x)
			{
				// Create a gold color by adjusting the r, g, b channels
				// Introduce some shininess by modulating the colors based on x and y
				uint8_t base_r = 200 + uint8_t((std::sin(x * 0.1f) + std::sin(y * 0.1f)) * 30.0f); // base red for golden color
				uint8_t base_g = 160 + uint8_t((std::sin(x * 0.1f) + std::sin(y * 0.1f)) * 30.0f); // base green
				uint8_t base_b = 50 + uint8_t((std::cos(x * 0.1f) + std::cos(y * 0.1f)) * 20.0f);  // base blue, lower to keep it yellowish

				// Apply metallic shininess by adding highlights:
				float shine = std::abs(std::sin(x * 0.05f + y * 0.05f)) * 80.0f;

				// Combine the base color with the shininess:
				uint8_t r = uint8_t(std::min(255.0f, base_r + shine));
				uint8_t g = uint8_t(std::min(255.0f, base_g + shine));
				uint8_t b = uint8_t(std::min(255.0f, base_b + shine));
				uint8_t a = 0xff; // fully opaque

				// Combine r, g, b, a into a single 32-bit value:
				data.emplace_back(uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24));
			}
		}

		assert(data.size() == size * size);

		// create the image on the GPU:
		textures.emplace_back(rtg.helpers.create_image(
			VkExtent2D{.width = size, .height = size}, // size of image
			VK_FORMAT_R8G8B8A8_SRGB,				   // 8-bit RGBA with SRGB encoding
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // sample and upload
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // device-local memory
			Helpers::Unmapped));

		// transfer the generated data to the GPU:
		rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
	}

	{ // texture 4 will be a metallic silver texture:
		// set the size of the texture:
		uint32_t size = 256;
		std::vector<uint32_t> data;
		data.reserve(size * size);

		// iterate over each pixel:
		for (uint32_t y = 0; y < size; ++y)
		{
			for (uint32_t x = 0; x < size; ++x)
			{
				// Create a silver color by adjusting the r, g, b channels
				// Introduce some shininess by modulating the colors based on x and y
				uint8_t base_r = 180 + uint8_t((std::sin(x * 0.1f) + std::sin(y * 0.1f)) * 40.0f); // base red for silver color
				uint8_t base_g = 180 + uint8_t((std::sin(x * 0.1f) + std::sin(y * 0.1f)) * 40.0f); // base green for silver
				uint8_t base_b = 180 + uint8_t((std::cos(x * 0.1f) + std::cos(y * 0.1f)) * 40.0f); // base blue for silver

				// Apply metallic shininess by adding highlights:
				float shine = std::abs(std::sin(x * 0.05f + y * 0.05f)) * 90.0f;

				// Combine the base color with the shininess:
				uint8_t r = uint8_t(std::min(255.0f, base_r + shine));
				uint8_t g = uint8_t(std::min(255.0f, base_g + shine));
				uint8_t b = uint8_t(std::min(255.0f, base_b + shine));
				uint8_t a = 0xff; // fully opaque

				// Combine r, g, b, a into a single 32-bit value:
				data.emplace_back(uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24));
			}
		}

		assert(data.size() == size * size);

		// create the image on the GPU:
		textures.emplace_back(rtg.helpers.create_image(
			VkExtent2D{.width = size, .height = size}, // size of image
			VK_FORMAT_R8G8B8A8_SRGB,				   // 8-bit RGBA with SRGB encoding
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // sample and upload
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // device-local memory
			Helpers::Unmapped));

		// transfer the generated data to the GPU:
		rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
	}

	{ // texture 5 will be a glass-like texture:
		// set the size of the texture:
		uint32_t size = 256;
		std::vector<uint32_t> data;
		data.reserve(size * size);

		// iterate over each pixel:
		for (uint32_t y = 0; y < size; ++y)
		{
			for (uint32_t x = 0; x < size; ++x)
			{
				// Create a glass-like color by adjusting the r, g, b channels
				// Use light blue/green tints to simulate slight coloring of glass
				uint8_t base_r = 180 + uint8_t((std::sin(x * 0.1f) + std::sin(y * 0.1f)) * 20.0f); // faint red tint
				uint8_t base_g = 200 + uint8_t((std::sin(x * 0.1f) + std::cos(y * 0.1f)) * 30.0f); // greenish tint
				uint8_t base_b = 220 + uint8_t((std::cos(x * 0.1f) + std::cos(y * 0.1f)) * 40.0f); // bluish tint

				// Apply shininess to simulate reflective glass surface:
				float shine = std::abs(std::sin(x * 0.05f + y * 0.05f)) * 60.0f;

				// Combine the base color with the shininess:
				uint8_t r = uint8_t(std::min(255.0f, base_r + shine));
				uint8_t g = uint8_t(std::min(255.0f, base_g + shine));
				uint8_t b = uint8_t(std::min(255.0f, base_b + shine));

				// Adjust alpha for transparency:
				uint8_t a = 128 + uint8_t((std::sin(x * 0.1f) + std::cos(y * 0.1f)) * 40.0f); // semi-transparent glass effect

				// Combine r, g, b, a into a single 32-bit value:
				data.emplace_back(uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24));
			}
		}

		assert(data.size() == size * size);

		// create the image on the GPU:
		textures.emplace_back(rtg.helpers.create_image(
			VkExtent2D{.width = size, .height = size}, // size of image
			VK_FORMAT_R8G8B8A8_UNORM,				   // 8-bit RGBA without SRGB encoding
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // sample and upload
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // device-local memory
			Helpers::Unmapped));

		// transfer the generated data to the GPU:
		rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
	}

	{ // texture 6 will be a ruby-like texture:
		// set the size of the texture:
		uint32_t size = 256;
		std::vector<uint32_t> data;
		data.reserve(size * size);

		// iterate over each pixel:
		for (uint32_t y = 0; y < size; ++y)
		{
			for (uint32_t x = 0; x < size; ++x)
			{
				// Create a ruby-like color by adjusting the r, g, b channels
				// Use deep red with slight variations for ruby appearance
				uint8_t base_r = 200 + uint8_t((std::sin(x * 0.1f) + std::sin(y * 0.1f)) * 40.0f); // strong red base color
				uint8_t base_g = 30 + uint8_t((std::sin(x * 0.1f) + std::cos(y * 0.1f)) * 20.0f);  // slight greenish tint
				uint8_t base_b = 30 + uint8_t((std::cos(x * 0.1f) + std::cos(y * 0.1f)) * 10.0f);  // slight bluish tint

				// Apply shininess to simulate the reflective ruby surface:
				float shine = std::abs(std::sin(x * 0.05f + y * 0.05f)) * 50.0f;

				// Combine the base color with the shininess:
				uint8_t r = uint8_t(std::min(255.0f, base_r + shine));
				uint8_t g = uint8_t(std::min(255.0f, base_g + shine));
				uint8_t b = uint8_t(std::min(255.0f, base_b + shine));

				// Adjust alpha for transparency to create a crystal-like effect:
				uint8_t a = 100 + uint8_t((std::sin(x * 0.1f) + std::cos(y * 0.1f)) * 60.0f); // semi-transparent ruby effect

				// Combine r, g, b, a into a single 32-bit value:
				data.emplace_back(uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24));
			}
		}

		assert(data.size() == size * size);

		// create the image on the GPU:
		textures.emplace_back(rtg.helpers.create_image(
			VkExtent2D{.width = size, .height = size}, // size of image
			VK_FORMAT_R8G8B8A8_UNORM,				   // 8-bit RGBA without SRGB encoding
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // sample and upload
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // device-local memory
			Helpers::Unmapped));

		// transfer the generated data to the GPU:
		rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
	}

	{ // make image views for the textures
		texture_views.reserve(textures.size());
		for (Helpers::AllocatedImage const &image : textures)
		{
			VkImageViewCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.flags = 0,
				.image = image.handle,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = image.format,
				// .components sets swizzling and is fine when zero-initialized
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
			};

			VkImageView image_view = VK_NULL_HANDLE;
			VK(vkCreateImageView(rtg.device, &create_info, nullptr, &image_view));

			texture_views.emplace_back(image_view);
		}
		assert(texture_views.size() == textures.size());
	}

	{ // make a sampler for the textures
		VkSamplerCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.flags = 0,
			.magFilter = VK_FILTER_NEAREST,
			.minFilter = VK_FILTER_NEAREST,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.mipLodBias = 0.0f,
			.anisotropyEnable = VK_FALSE,
			.maxAnisotropy = 0.0f, // doesn't matter if anisotropy isn't enabled
			.compareEnable = VK_FALSE,
			.compareOp = VK_COMPARE_OP_ALWAYS, // doesn't matter if compare isn't enabled
			.minLod = 0.0f,
			.maxLod = 0.0f,
			.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			.unnormalizedCoordinates = VK_FALSE,
		};
		VK(vkCreateSampler(rtg.device, &create_info, nullptr, &texture_sampler));
	}

	{
		// create the texture descriptor pool
		uint32_t per_texture = uint32_t(textures.size()); // for easier-to-read counting

		std::array<VkDescriptorPoolSize, 1> pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1 * 1 * per_texture, // one descriptor per set, one set per texture
			},
		};

		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0,					// because CREATE_FREE_DESCRIPTOR_SET_BIT isn't included, *can't* free individual descriptors allocated from this pool
			.maxSets = 1 * per_texture, // one set per texture
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &texture_descriptor_pool));
	}

	{ // allocate and write the texture descriptor sets

		// allocate the descriptors (using the same alloc_info):
		VkDescriptorSetAllocateInfo alloc_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = texture_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &objects_pipeline.set2_TEXTURE,
		};
		texture_descriptors.assign(textures.size(), VK_NULL_HANDLE);
		for (VkDescriptorSet &descriptor_set : texture_descriptors)
		{
			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &descriptor_set));
		}

		// write descriptors for textures:
		std::vector<VkDescriptorImageInfo> infos(textures.size());
		std::vector<VkWriteDescriptorSet> writes(textures.size());

		for (Helpers::AllocatedImage const &image : textures)
		{
			size_t i = &image - &textures[0];

			infos[i] = VkDescriptorImageInfo{
				.sampler = texture_sampler,
				.imageView = texture_views[i],
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			writes[i] = VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = texture_descriptors[i],
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &infos[i],
			};
		}

		vkUpdateDescriptorSets(rtg.device, uint32_t(writes.size()), writes.data(), 0, nullptr);
	}
}

Tutorial::~Tutorial()
{
	// just in case rendering is still in flight, don't destroy resources:
	//(not using VK macro to avoid throw-ing in destructor)
	if (VkResult result = vkDeviceWaitIdle(rtg.device); result != VK_SUCCESS)
	{
		std::cerr << "Failed to vkDeviceWaitIdle in Tutorial::~Tutorial [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
	}

	if (texture_descriptor_pool)
	{
		vkDestroyDescriptorPool(rtg.device, texture_descriptor_pool, nullptr);
		texture_descriptor_pool = nullptr;

		// this also frees the descriptor sets allocated from the pool:
		texture_descriptors.clear();
	}

	if (texture_sampler)
	{
		vkDestroySampler(rtg.device, texture_sampler, nullptr);
		texture_sampler = VK_NULL_HANDLE;
	}

	for (VkImageView &view : texture_views)
	{
		vkDestroyImageView(rtg.device, view, nullptr);
		view = VK_NULL_HANDLE;
	}
	texture_views.clear();

	for (auto &texture : textures)
	{
		rtg.helpers.destroy_image(std::move(texture));
	}
	textures.clear();

	rtg.helpers.destroy_buffer(std::move(object_vertices));

	if (swapchain_depth_image.handle != VK_NULL_HANDLE)
	{
		destroy_framebuffers();
	}

	background_pipeline.destroy(rtg);
	lines_pipeline.destroy(rtg);
	objects_pipeline.destroy(rtg);

	for (Workspace &workspace : workspaces)
	{
		refsol::Tutorial_destructor_workspace(rtg, command_pool, &workspace.command_buffer);
		if (workspace.lines_vertices_src.handle != VK_NULL_HANDLE)
		{
			rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices_src));
		}
		if (workspace.lines_vertices.handle != VK_NULL_HANDLE)
		{
			rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices));
		}

		if (workspace.Camera_src.handle != VK_NULL_HANDLE)
		{
			rtg.helpers.destroy_buffer(std::move(workspace.Camera_src));
		}
		if (workspace.Camera.handle != VK_NULL_HANDLE)
		{
			rtg.helpers.destroy_buffer(std::move(workspace.Camera));
		}
		// Camera_descriptors freed when pool is destroyed.

		if (workspace.World_src.handle != VK_NULL_HANDLE)
		{
			rtg.helpers.destroy_buffer(std::move(workspace.World_src));
		}
		if (workspace.World.handle != VK_NULL_HANDLE)
		{
			rtg.helpers.destroy_buffer(std::move(workspace.World));
		}
		// World_descriptors freed when pool is destroyed.

		if (workspace.Transforms_src.handle != VK_NULL_HANDLE)
		{
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms_src));
		}
		if (workspace.Transforms.handle != VK_NULL_HANDLE)
		{
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms));
		}
		// Transforms_descriptors freed when pool is destroyed.
	}
	workspaces.clear();

	if (descriptor_pool)
	{
		vkDestroyDescriptorPool(rtg.device, descriptor_pool, nullptr);
		descriptor_pool = nullptr;
		//(this also frees the descriptor sets allocated from the pool)
	}

	refsol::Tutorial_destructor(rtg, &render_pass, &command_pool);
}

void Tutorial::on_swapchain(RTG &rtg_, RTG::SwapchainEvent const &swapchain)
{
	//[re]create framebuffers:
	refsol::Tutorial_on_swapchain(rtg, swapchain, depth_format, render_pass, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
}

void Tutorial::destroy_framebuffers()
{
	refsol::Tutorial_destroy_framebuffers(rtg, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
}

void Tutorial::render(RTG &rtg_, RTG::RenderParams const &render_params)
{
	// assert that parameters are valid:
	assert(&rtg == &rtg_);
	assert(render_params.workspace_index < workspaces.size());
	assert(render_params.image_index < swapchain_framebuffers.size());

	// get more convenient names for the current workspace and target framebuffer:
	Workspace &workspace = workspaces[render_params.workspace_index];
	[[maybe_unused]] VkFramebuffer framebuffer = swapchain_framebuffers[render_params.image_index];

	// record (into `workspace.command_buffer`) commands that run a `render_pass` that just clears `framebuffer`:
	// refsol::Tutorial_render_record_blank_frame(rtg, render_pass, framebuffer, &workspace.command_buffer);

	// reset the command buffer (clear old commands):
	VK(vkResetCommandBuffer(workspace.command_buffer, 0));

	{ // begin recording:
		VkCommandBufferBeginInfo begin_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			//.pNext set to nullptr by zero-initialization!
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, // will record again every submit
		};
		VK(vkBeginCommandBuffer(workspace.command_buffer, &begin_info));
	}

	if (!lines_vertices.empty())
	{ // upload lines vertices:
		//[re-]allocate lines buffers if needed:
		size_t needed_bytes = lines_vertices.size() * sizeof(lines_vertices[0]);
		if (workspace.lines_vertices_src.handle == VK_NULL_HANDLE || workspace.lines_vertices_src.size < needed_bytes)
		{
			// round to next multiple of 4k to avoid re-allocating continuously if vertex count grows slowly:
			size_t new_bytes = ((needed_bytes + 4096) / 4096) * 4096;

			if (workspace.lines_vertices_src.handle)
			{
				rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices_src));
			}
			if (workspace.lines_vertices.handle)
			{
				rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices));
			}

			workspace.lines_vertices_src = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,											// going to have GPU copy from this memory
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // host-visible memory, coherent (no special sync needed)
				Helpers::Mapped																// get a pointer to the memory
			);
			workspace.lines_vertices = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, // going to use as vertex buffer, also going to have GPU into this memory
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,								  // GPU-local memory
				Helpers::Unmapped													  // don't get a pointer to the memory
			);

			std::cout << render_params.workspace_index
					  << ": Re-allocated lines buffers to " << new_bytes << " bytes." << std::endl;
		}

		assert(workspace.lines_vertices_src.size == workspace.lines_vertices.size);
		assert(workspace.lines_vertices_src.size >= needed_bytes);

		// host-side copy into lines_vertices_src:
		assert(workspace.lines_vertices_src.allocation.mapped);
		std::memcpy(workspace.lines_vertices_src.allocation.data(), lines_vertices.data(), needed_bytes);

		// device-side copy from lines_vertices_src -> lines_vertices:
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = needed_bytes,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.lines_vertices_src.handle, workspace.lines_vertices.handle, 1, &copy_region);
	}

	{ // upload camera info:
		LinesPipeline::Camera camera{
			.CLIP_FROM_WORLD = CLIP_FROM_WORLD};
		assert(workspace.Camera_src.size == sizeof(camera));

		// host-side copy into Camera_src:
		memcpy(workspace.Camera_src.allocation.data(), &camera, sizeof(camera));

		// add device-side copy from Camera_src -> Camera:
		assert(workspace.Camera_src.size == workspace.Camera.size);
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = workspace.Camera_src.size,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.Camera_src.handle, workspace.Camera.handle, 1, &copy_region);
	}

	{ // upload world info:
		assert(workspace.Camera_src.size == sizeof(world));

		// host-side copy into World_src:
		memcpy(workspace.World_src.allocation.data(), &world, sizeof(world));

		// add device-side copy from World_src -> World:
		assert(workspace.World_src.size == workspace.World.size);
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = workspace.World_src.size,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.World_src.handle, workspace.World.handle, 1, &copy_region);
	}

	if (!object_instances.empty())
	{ // upload object transforms:
		//[re-]allocate lines buffers if needed:
		size_t needed_bytes = object_instances.size() * sizeof(ObjectsPipeline::Transform);
		if (workspace.Transforms_src.handle == VK_NULL_HANDLE || workspace.Transforms_src.size < needed_bytes)
		{
			// round to next multiple of 4k to avoid re-allocating continuously if vertex count grows slowly:
			size_t new_bytes = ((needed_bytes + 4096) / 4096) * 4096;

			if (workspace.Transforms_src.handle)
			{
				rtg.helpers.destroy_buffer(std::move(workspace.Transforms_src));
			}
			if (workspace.Transforms.handle)
			{
				rtg.helpers.destroy_buffer(std::move(workspace.Transforms));
			}

			workspace.Transforms_src = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,											// going to have GPU copy from this memory
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // host-visible memory, coherent (no special sync needed)
				Helpers::Mapped																// get a pointer to the memory
			);
			workspace.Transforms = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, // going to use as vertex buffer, also going to have GPU into this memory
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,								   // GPU-local memory
				Helpers::Unmapped													   // don't get a pointer to the memory
			);

			// update the descriptor set:
			VkDescriptorBufferInfo Transforms_info{
				.buffer = workspace.Transforms.handle,
				.offset = 0,
				.range = workspace.Transforms.size,
			};

			std::array<VkWriteDescriptorSet, 1> writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Transforms_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &Transforms_info,
				},
			};

			vkUpdateDescriptorSets(
				rtg.device,
				uint32_t(writes.size()), writes.data(), // descriptorWrites count, data
				0, nullptr								// descriptorCopies count, data
			);

			std::cout << "Re-allocated object transforms buffers to " << new_bytes << " bytes." << std::endl;
		}

		assert(workspace.Transforms_src.size == workspace.Transforms.size);
		assert(workspace.Transforms_src.size >= needed_bytes);

		{ // copy transforms into Transforms_src:
			assert(workspace.Transforms_src.allocation.mapped);
			ObjectsPipeline::Transform *out = reinterpret_cast<ObjectsPipeline::Transform *>(workspace.Transforms_src.allocation.data()); // Strict aliasing violation, but it doesn't matter
			for (ObjectInstance const &inst : object_instances)
			{
				*out = inst.transform;
				++out;
			}
		}

		// device-side copy from lines_vertices_src -> lines_vertices:
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = needed_bytes,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.Transforms_src.handle, workspace.Transforms.handle, 1, &copy_region);
	}

	{ // memory barrier to make sure copies complete before rendering happens:
		VkMemoryBarrier memory_barrier{
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
		};

		vkCmdPipelineBarrier(workspace.command_buffer,
							 VK_PIPELINE_STAGE_TRANSFER_BIT,	 // srcStageMask
							 VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, // dstStageMask
							 0,									 // dependencyFlags
							 1, &memory_barrier,				 // memoryBarriers (count, data)
							 0, nullptr,						 // bufferMemoryBarriers (count, data)
							 0, nullptr							 // imageMemoryBarriers (count, data)
		);
	}

	// put GPU commands here!
	{ // render pass
		std::array<VkClearValue, 2> clear_values{
			VkClearValue{.color{.float32{0.851f, 0.902f, 0.3137f, 1.0f}}},
			VkClearValue{.depthStencil{.depth = 1.0f, .stencil = 0}},
		};

		VkRenderPassBeginInfo begin_info{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = render_pass,
			.framebuffer = framebuffer,
			.renderArea{
				.offset = {.x = 0, .y = 0},
				.extent = rtg.swapchain_extent,
			},
			.clearValueCount = uint32_t(clear_values.size()),
			.pClearValues = clear_values.data(),
		};

		vkCmdBeginRenderPass(workspace.command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

		// run pipelines here
		{
			// set scissor rectangle:
			VkRect2D scissor{
				.offset = {.x = 0, .y = 0},
				.extent = rtg.swapchain_extent,
			};
			vkCmdSetScissor(workspace.command_buffer, 0, 1, &scissor);
		}
		{
			// configure viewport transform
			VkViewport viewport{
				.x = 0.0f,
				.y = 0.0f,
				.width = float(rtg.swapchain_extent.width),
				.height = float(rtg.swapchain_extent.height),
				.minDepth = 0.0f,
				.maxDepth = 1.0f,
			};
			vkCmdSetViewport(workspace.command_buffer, 0, 1, &viewport);
		}

		{
			// draw with the background pipeline:
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, background_pipeline.handle);

			{ // push time:
				BackgroundPipeline::Push push{
					.time = float(time),
				};
				vkCmdPushConstants(workspace.command_buffer, background_pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
			}

			vkCmdDraw(workspace.command_buffer, 3, 1, 0, 0);
		}

		{ // draw with the lines pipeline:
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lines_pipeline.handle);

			{ // use lines_vertices (offset 0) as vertex buffer binding 0:
				std::array<VkBuffer, 1> vertex_buffers{workspace.lines_vertices.handle};
				std::array<VkDeviceSize, 1> offsets{0};
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
			}

			{ // bind Camera descriptor set:
				std::array<VkDescriptorSet, 1> descriptor_sets{
					workspace.Camera_descriptors, // 0: Camera
				};
				vkCmdBindDescriptorSets(
					workspace.command_buffer,								  // command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS,						  // pipeline bind point
					lines_pipeline.layout,									  // pipeline layout
					0,														  // first set
					uint32_t(descriptor_sets.size()), descriptor_sets.data(), // descriptor sets count, ptr
					0, nullptr												  // dynamic offsets count, ptr
				);
			}

			// draw lines vertices:
			// vkCmdDraw(workspace.command_buffer, uint32_t(lines_vertices.size()), 1, 0, 0);
		}

		if (!object_instances.empty())
		{ // draw with the objects pipeline:
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, objects_pipeline.handle);

			{ // use object_vertices (offset 0) as vertex buffer binding 0:
				std::array<VkBuffer, 1> vertex_buffers{object_vertices.handle};
				std::array<VkDeviceSize, 1> offsets{0};
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
			}

			{ // bind World and Transforms descriptor sets:
				std::array<VkDescriptorSet, 2> descriptor_sets{
					workspace.World_descriptors,	  // 0: World
					workspace.Transforms_descriptors, // 1: Transforms
				};
				vkCmdBindDescriptorSets(
					workspace.command_buffer,								  // command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS,						  // pipeline bind point
					objects_pipeline.layout,								  // pipeline layout
					0,														  // first set
					uint32_t(descriptor_sets.size()), descriptor_sets.data(), // descriptor sets count, ptr
					0, nullptr												  // dynamic offsets count, ptr
				);
			}

			// Camera descriptor set is still bound, but unused(!)

			// draw all instances:
			for (ObjectInstance const &inst : object_instances)
			{
				uint32_t index = uint32_t(&inst - &object_instances[0]);

				// bind texture descriptor set:
				vkCmdBindDescriptorSets(
					workspace.command_buffer,			   // command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS,	   // pipeline bind point
					objects_pipeline.layout,			   // pipeline layout
					2,									   // second set
					1, &texture_descriptors[inst.texture], // descriptor sets count, ptr
					0, nullptr							   // dynamic offsets count, ptr
				);

				vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
			}

			// draw torus vertices:
			// vkCmdDraw(workspace.command_buffer, 2 * torus_vertices.count, 2, torus_vertices.first, 0);
		}

		vkCmdEndRenderPass(workspace.command_buffer);
	}

	// end recording:
	VK(vkEndCommandBuffer(workspace.command_buffer));

	// submit `workspace.command buffer` for the GPU to run:
	refsol::Tutorial_render_submit(rtg, render_params, workspace.command_buffer);
}

void Tutorial::update(float dt)
{
	time = std::fmod(time + dt, 60.0f);

	{ // camera orbiting the origin:
		[[maybe_unused]] float ang = float(M_PI) * 2.0f * 6.0f * (time / 60.0f);
		CLIP_FROM_WORLD = perspective(
							  60.0f / float(M_PI) * 180.0f,									   // vfov
							  rtg.swapchain_extent.width / float(rtg.swapchain_extent.height), // aspect
							  0.1f,															   // near
							  1000.0f														   // far
							  ) *
						  look_at(
							  3.0f * std::cos(ang), 3.0f * std::sin(ang), -1.f * std::cos(ang), // eye
							  // 3.0f, 3.0f, 1.0f,
							  0.0f, 0.2f, 0.5f * std::sin(ang), // target
							  0.f, .5f, 0.5f					// up
						  );
	}

	{
		// Define time-based variable for smooth transitions
		float t = std::fmod(time, 60.0f) / 20.0f;

		// Use time to create rainbow-like colors (HSV to RGB conversion could also be used)
		float red = 0.5f + 0.5f * std::sin(t * 2.0f * 3.14f + 0.0f);   // Red
		float green = 0.5f + 0.5f * std::sin(t * 2.0f * 3.14f + 2.0f); // Green
		float blue = 0.5f + 0.5f * std::sin(t * 2.0f * 3.14f + 4.0f);  // Blue

		// Static sky direction (for now, you can rotate or move it to simulate dynamic weather)
		world.SKY_DIRECTION.x = 0.0f;
		world.SKY_DIRECTION.y = 0.0f;
		world.SKY_DIRECTION.z = 1.0f;

		// Set environment light color (dynamic rainbow effect)
		world.SKY_ENERGY.r = red;	// Red channel
		world.SKY_ENERGY.g = green; // Green channel
		world.SKY_ENERGY.b = blue;	// Blue channel

		// Optionally adjust intensity or distribution across the sky:
		float intensity = 0.2f + 0.8f * std::sin(t * 3.14f); // Flicker intensity like sunlight through clouds

		// Modify sun light to be less dominant and emphasize rainbow light
		world.SUN_DIRECTION.x = 6.0f / 23.0f;
		world.SUN_DIRECTION.y = 13.0f / 23.0f;
		world.SUN_DIRECTION.z = 18.0f / 23.0f;

		world.SUN_ENERGY.r = intensity * 0.5f;
		world.SUN_ENERGY.g = intensity * 0.5f;
		world.SUN_ENERGY.b = intensity * 0.5f;
	}

	{ // make some crossing lines at different depths:
		lines_vertices.clear();
		constexpr size_t count = 2 * 30 + 2 * 30;
		lines_vertices.reserve(count);
		// horizontal lines at z = 0.5f:
		for (uint32_t i = 0; i < 30; ++i)
		{
			float y_base = (i + 0.5f) / 30.0f * 2.0f - 1.0f;
			float z = (i + 0.5f) / 30.0f;
			float y_offset = std::sin(time + z * 3.14159f * 2.0f) * 0.2f; // Adjust the amplitude and frequency as needed

			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = -1.0f, .y = y_base + y_offset, .z = 0.5f},
				.Color{.r = 0x1f, .g = 0xff, .b = 0x00, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = 1.0f, .y = y_base - y_offset, .z = 0.5f},
				.Color{.r = 0x1f, .g = 0x0f, .b = 0xf0, .a = 0xff},
			});
		}
		// vertical lines at z = 0.0f (near) through 1.0f (far):
		for (uint32_t i = 0; i < 30; ++i)
		{
			float x_base = (i + 0.5f) / 30.0f * 2.0f - 1.0f;
			float z = (i + 0.5f) / 30.0f;
			float x_offset = std::sin(time + z * 3.14159f * 2.0f) * 0.2f; // Adjust the amplitude and frequency as needed

			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = x_base + x_offset, .y = -1.0f, .z = z},
				.Color{.r = 0x04, .g = 0x00, .b = 0x0f, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = x_base - x_offset, .y = 1.0f, .z = z},
				.Color{.r = 0x04, .g = 0x00, .b = 0xff, .a = 0xff},
			});
		}
		assert(lines_vertices.size() == count);
	}

	{
		lines_vertices.clear();
		constexpr size_t count = 2 * 30 * 30; // Number of lines (latitude * longitude)
		lines_vertices.reserve(count);

		// Constants for the sphere
		const float radius = 1.0f;
		float e = 1.0f + 0.5f * std::sin(time);
		const size_t num_latitude_lines = 30;
		const size_t num_longitude_lines = 30;

		// Latitude lines (parallel to the equator)
		for (uint32_t lat = 0; lat < num_latitude_lines; ++lat)
		{
			float theta = (lat + 0.5f) / num_latitude_lines * 3.14159f; // From 0 to Pi (180 degrees)
			for (uint32_t lon = 0; lon < num_longitude_lines; ++lon)
			{
				float phi = lon / (float)num_longitude_lines * 2.0f * 3.14159f; // From 0 to 2*Pi (360 degrees)

				float x = radius * std::sin(theta) * std::cos(phi);
				float y = radius * std::sin(theta) * std::sin(phi);
				float z = radius * std::cos(theta) / e; // Adjust z for ellipsoid

				float x_next = radius * std::sin(theta) * std::cos(phi + 2.0f * 3.14159f / num_longitude_lines);
				float y_next = radius * std::sin(theta) * std::sin(phi + 2.0f * 3.14159f / num_longitude_lines);

				lines_vertices.emplace_back(PosColVertex{
					.Position{.x = x, .y = y, .z = z},
					.Color{.r = 0x1f, .g = 0xff, .b = 0x00, .a = 0xff},
				});
				lines_vertices.emplace_back(PosColVertex{
					.Position{.x = x_next, .y = y_next, .z = z},
					.Color{.r = 0x1f, .g = 0xff, .b = 0x00, .a = 0xff},
				});
			}
		}

		// Longitude lines (from pole to pole)
		for (uint32_t lon = 0; lon < num_longitude_lines; ++lon)
		{
			float phi = lon / (float)num_longitude_lines * 2.0f * 3.14159f; // From 0 to 2*Pi (360 degrees)
			for (uint32_t lat = 0; lat < num_latitude_lines; ++lat)
			{
				float theta = (lat + 0.5f) / num_latitude_lines * 3.14159f; // From 0 to Pi (180 degrees)

				float x = radius * std::sin(theta) * std::cos(phi);
				float y = radius * std::sin(theta) * std::sin(phi);
				float z = radius * std::cos(theta) / e; // Adjust z for ellipsoid

				float z_next = radius * std::cos(theta + 3.14159f / num_latitude_lines) / e;

				lines_vertices.emplace_back(PosColVertex{
					.Position{.x = x, .y = y, .z = z},
					.Color{.r = 0xf4, .g = 0xf0, .b = 0xff, .a = 0xff},
				});
				lines_vertices.emplace_back(PosColVertex{
					.Position{.x = radius * std::sin(theta + 3.14159f / num_latitude_lines) * std::cos(phi),
							  .y = radius * std::sin(theta + 3.14159f / num_latitude_lines) * std::sin(phi),
							  .z = z_next},
					.Color{.r = 0x04, .g = 0x80, .b = 0x7f, .a = 0xff},
				});
			}
		}

		// assert(lines_vertices.size() == count);
	}

	{ // make some objects:
		object_instances.clear();

		{ // plane translated +x by one unit:
			mat4 WORLD_FROM_LOCAL{
				1.0f,
				0.0f,
				0.0f,
				0.0f,
				0.0f,
				1.0f,
				0.0f,
				0.0f,
				0.0f,
				0.0f,
				1.0f,
				0.0f,
				0.0f,
				0.0f,
				0.0f,
				1.0f,
			};

			object_instances.emplace_back(ObjectInstance{
				.vertices = plane_vertices,
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
				},
			});
		}

		{ // torus translated -x by one unit and rotated CCW around +y:
			float ang = time / 60.0f * 2.0f * float(M_PI) * 10.0f;
			float ca = std::cos(ang);
			float sa = std::sin(ang);
			mat4 WORLD_FROM_LOCAL{
				ca,
				0.0f,
				-sa,
				0.0f,
				0.0f,
				1.0f,
				0.0f,
				0.0f,
				sa,
				0.0f,
				ca,
				0.0f,
				0.0f,
				0.0f,
				0.0f,
				1.0f};

			// Create rotation matrix for the x-axis
			float angle_x = time / 60.0f * 2.0f * float(M_PI) * 10.0f; // Adjust time to control the speed of rotation
			float cx = std::cos(angle_x);
			float sx = std::sin(angle_x);
			float cx_1 = std::cos(3.f * angle_x);
			float sx_1 = std::sin(3.f * angle_x);

			// Create rotation matrix for the y-axis
			float angle_y = time / 60.0f * 2.0f * float(M_PI) * 18.0f; // Adjust time for rotation speed
			float cy = std::cos(angle_y);
			float sy = std::sin(angle_y);
			// Create rotation matrix for the z-axis
			float angle_z = time / 60.0f * 2.0f * float(M_PI) * 25.0f; // Adjust time for rotation speed
			float cz = std::cos(angle_z);
			float sz = std::sin(angle_z);

			mat4 ROTATE_X{
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, cx, -sx, 0.0f,
				0.0f, sx, cx, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f};

			mat4 ROTATE_X_1{
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, cx_1, -sx_1, 0.0f,
				0.0f, sx_1, cx_1, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f};

			mat4 ROTATE_Y{
				cy, 0.0f, sy, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				-sy, 0.0f, cy, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f};

			mat4 ROTATE_Z{
				cz, -sz, 0.0f, 0.0f,
				sz, cz, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f};

			// Combine the rotations (apply in the desired order)
			mat4 WORLD_FROM_LOCAL_0 = ROTATE_X * WORLD_FROM_LOCAL;	 // Rotation about X-axis
			mat4 WORLD_FROM_LOCAL_1 = ROTATE_Y * WORLD_FROM_LOCAL;	 // Rotation about Y-axis
			mat4 WORLD_FROM_LOCAL_2 = ROTATE_Z * WORLD_FROM_LOCAL;	 // Rotation about Z-axis
			mat4 WORLD_FROM_LOCAL_3 = ROTATE_X_1 * WORLD_FROM_LOCAL; // Rotation about X-axis

			object_instances.emplace_back(ObjectInstance{
				.vertices = torus_vertices,
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL_0,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL_0,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL_0,
				},
				.texture = 2,
			});

			object_instances.emplace_back(ObjectInstance{
				.vertices = torus_vertices_1,
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL_1,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL_1,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL_1,
				},
				.texture = 4,
			});

			object_instances.emplace_back(ObjectInstance{
				.vertices = torus_vertices_2,
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL_2,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL_2,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL_2,
				},
				.texture = 3,
			});

			object_instances.emplace_back(ObjectInstance{
				.vertices = torus_vertices_3,
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL_3,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL_3,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL_3,
				},
				.texture = 5,
			});

			object_instances.emplace_back(ObjectInstance{
				.vertices = tetrahedron,
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL_1,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL_1,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL_1,
				},
				.texture = 6,
			});
		}
	}
}

void Tutorial::on_input(InputEvent const &)
{
}

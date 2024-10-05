#ifdef _WIN32
// ensure we have M_PI
#define _USE_MATH_DEFINES
#endif

#include "Tutorial.hpp"
#include "scene.hpp"
#include "helper/VK.hpp"
#include <vulkan/vk_enum_string_helper.h>
#include "GLFW\glfw3.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <chrono>

#include <filesystem>

#include "include/sejp/sejp.hpp"
#include "lib/bbox.h"

std::chrono::time_point<std::chrono::high_resolution_clock> start, end;

Tutorial::Tutorial(RTG &rtg_) : rtg(rtg_)
{
	// select a depth format:
	//   (at least one of these two must be supported, according to the spec; but neither are required)
	depth_format = rtg.helpers.find_image_format(
		{VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32},
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

	// in Tutorial::Tutorial:
	{ // create render pass
		std::array<VkAttachmentDescription, 2> attachments{
			VkAttachmentDescription{
				// 0 - color attachment:
				.format = rtg.surface_format.format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			},
			VkAttachmentDescription{
				// 1 - depth attachment:
				.format = depth_format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			},
		};

		if (rtg.configuration.headless)
		{
			attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
		}

		// subpass
		VkAttachmentReference color_attachment_ref{
			.attachment = 0,
			.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		};

		VkAttachmentReference depth_attachment_ref{
			.attachment = 1,
			.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};

		VkSubpassDescription subpass{
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount = 0,
			.pInputAttachments = nullptr,
			.colorAttachmentCount = 1,
			.pColorAttachments = &color_attachment_ref,
			.pDepthStencilAttachment = &depth_attachment_ref,
		};

		// dependencies
		// this defers the image load actions for the attachments:
		std::array<VkSubpassDependency, 2> dependencies{
			VkSubpassDependency{
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			},
			VkSubpassDependency{
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			}};

		VkRenderPassCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = uint32_t(attachments.size()),
			.pAttachments = attachments.data(),
			.subpassCount = 1,
			.pSubpasses = &subpass,
			.dependencyCount = uint32_t(dependencies.size()),
			.pDependencies = dependencies.data(),
		};

		VK(vkCreateRenderPass(rtg.device, &create_info, nullptr, &render_pass));
	}

	{ // create command pool
		VkCommandPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = rtg.graphics_queue_family.value(),
		};
		VK(vkCreateCommandPool(rtg.device, &create_info, nullptr, &command_pool));
	}

	// load scene file .72
	load_s72();

	if (!rtg.configuration.headless)
	{
		// background_pipeline.create(rtg, render_pass, 0);
		lines_pipeline.create(rtg, render_pass, 0);
		objects_pipeline.create(rtg, render_pass, 0);
	}

	scenes_pipeline.create(rtg, render_pass, 0);

	// create descriptor pool:
	{
		uint32_t per_workspace = uint32_t(rtg.workspaces.size()); // for easier-to-read counting

		std::array<VkDescriptorPoolSize, 2> pool_sizes{
			VkDescriptorPoolSize{
				// for camera
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 3 * per_workspace, // 3 descriptor per set, one set per workspace
			},
			VkDescriptorPoolSize{
				// for transform
				.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 2 * per_workspace, // 2 descriptor per set, one set per workspace
			},
		};

		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0,					  // because CREATE_FREE_DESCRIPTOR_SET_BIT isn't included, *can't* free individual descriptors allocated from this pool
			.maxSets = 6 * per_workspace, // three set per workspace
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool));
	}

	workspaces.resize(rtg.workspaces.size());
	std::cout << "\nworkspace size:" << workspaces.size() << "\n";

	for (Workspace &workspace : workspaces)
	{
		{ // allocate command buffer:
			VkCommandBufferAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool = command_pool,
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1,
			};
			VK(vkAllocateCommandBuffers(rtg.device, &alloc_info, &workspace.command_buffer));
		}

		if (!rtg.configuration.headless)
		{
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
		}

		workspace.Scene_world_src = rtg.helpers.create_buffer(
			sizeof(ScenesPipeline::World),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,											// going to have GPU copy from this memory
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // host-visible memory, coherent (no special sync needed)
			Helpers::Mapped																// get a pointer to the memory
		);
		workspace.Scene_world = rtg.helpers.create_buffer(
			sizeof(ScenesPipeline::World),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, // going to use as a uniform buffer, also going to have GPU copy into this memory
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,								   // GPU-local memory
			Helpers::Unmapped													   // don't get a pointer to the memory
		);

		{ // allocate descriptor set for Scene_camera descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &scenes_pipeline.set0_World,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Scene_world_descriptors));
		}

		{ // allocate descriptor set for Transforms descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &scenes_pipeline.set1_Transforms,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Scene_transforms_descriptors));
			// NOTE: will fill in this descriptor set in render when buffers are [re-]allocated
		}

		{ // point descriptor to buffer:
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

			VkDescriptorBufferInfo Scene_world_info{
				.buffer = workspace.Scene_world.handle,
				.offset = 0,
				.range = workspace.Scene_world.size,
			};

			std::vector<VkWriteDescriptorSet> writes;

			if (!rtg.configuration.headless)
			{
				{
					writes.emplace_back(VkWriteDescriptorSet{
						.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						.dstSet = workspace.Camera_descriptors,
						.dstBinding = 0,
						.dstArrayElement = 0,
						.descriptorCount = 1,
						.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
						.pBufferInfo = &Camera_info,
					});
					writes.emplace_back(VkWriteDescriptorSet{
						.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						.dstSet = workspace.World_descriptors,
						.dstBinding = 0,
						.dstArrayElement = 0,
						.descriptorCount = 1,
						.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
						.pBufferInfo = &World_info,
					});
				};
			}
			writes.emplace_back(VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = workspace.Scene_world_descriptors,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pBufferInfo = &Scene_world_info,
			});

			vkUpdateDescriptorSets(
				rtg.device,				 // device
				uint32_t(writes.size()), // descriptorWriteCount
				writes.data(),			 // pDescriptorWrites
				0,						 // descriptorCopyCount
				nullptr					 // pDescriptorCopies
			);
		}
	}
	if (!rtg.configuration.headless)
	{
		{ // create object vertices
			std::vector<PosNorTexVertex> vertices;

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

			size_t bytes = vertices.size() * sizeof(vertices[0]);

			object_vertices = rtg.helpers.create_buffer(
				bytes,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped);

			// copy data to buffer:
			// rtg.helpers.transfer_to_buffer(vertices.data(), bytes, object_vertices);
		}
	}
	{
		// create scene object vertices
		std::vector<SceneVertex> vertices;

		load_vertex_from_b72(vertices);

		size_t bytes = vertices.size() * sizeof(vertices[0]);

		scene_vertices = rtg.helpers.create_buffer(
			bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped);

		std::cout << "\nSSize: " << bytes << ", " << vertices.size() << " * " << sizeof(vertices[0]) << "\n\n";
		// copy data to buffer:
		rtg.helpers.transfer_to_buffer(vertices.data(), bytes, scene_vertices);
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
			.pSetLayouts = &scenes_pipeline.set2_TEXTURE,
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
	// else
	// {
	// 	size_t bytes = headless_pipeline.computeInput.size() * sizeof(uint32_t);

	// 	headless_resource = rtg.helpers.create_buffer(
	// 		bytes,
	// 		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	// 		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	// 		Helpers::Unmapped);

	// 	// copy data to buffer:
	// 	rtg.helpers.transfer_to_buffer(headless_pipeline.computeInput.data(), bytes, headless_resource);
	// }

	start = std::chrono::high_resolution_clock::now();
	end = std::chrono::high_resolution_clock::now();
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
	rtg.helpers.destroy_buffer(std::move(scene_vertices));

	if (swapchain_depth_image.handle != VK_NULL_HANDLE)
	{
		destroy_framebuffers();
	}

	// background_pipeline.destroy(rtg);
	lines_pipeline.destroy(rtg);
	objects_pipeline.destroy(rtg);
	scenes_pipeline.destroy(rtg);

	for (Workspace &workspace : workspaces)
	{
		if (workspace.command_buffer != VK_NULL_HANDLE)
		{
			vkFreeCommandBuffers(rtg.device, command_pool, 1, &workspace.command_buffer);
			workspace.command_buffer = VK_NULL_HANDLE;
		}

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

		if (workspace.Scene_world_src.handle != VK_NULL_HANDLE)
		{
			rtg.helpers.destroy_buffer(std::move(workspace.Scene_world_src));
		}
		if (workspace.Scene_world.handle != VK_NULL_HANDLE)
		{
			rtg.helpers.destroy_buffer(std::move(workspace.Scene_world));
		}

		if (workspace.Scene_transforms_src.handle != VK_NULL_HANDLE)
		{
			rtg.helpers.destroy_buffer(std::move(workspace.Scene_transforms_src));
		}
		if (workspace.Scene_transforms.handle != VK_NULL_HANDLE)
		{
			rtg.helpers.destroy_buffer(std::move(workspace.Scene_transforms));
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

	// destroy command pool
	if (command_pool != VK_NULL_HANDLE)
	{
		vkDestroyCommandPool(rtg.device, command_pool, nullptr);
		command_pool = VK_NULL_HANDLE;
	}

	if (render_pass != VK_NULL_HANDLE)
	{
		vkDestroyRenderPass(rtg.device, render_pass, nullptr);
		render_pass = VK_NULL_HANDLE;
	}
}

void Tutorial::on_swapchain(RTG &rtg_, RTG::SwapchainEvent const &swapchain)
{
	// clean up existing framebuffers (and depth image):
	if (swapchain_depth_image.handle != VK_NULL_HANDLE)
	{
		destroy_framebuffers();
	}

	// Allocate depth image for framebuffers to share:
	swapchain_depth_image = rtg.helpers.create_image(
		swapchain.extent,
		depth_format,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		Helpers::Unmapped);

	{ // create depth image view:
		VkImageViewCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = swapchain_depth_image.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = depth_format,
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1},
		};

		VK(vkCreateImageView(rtg.device, &create_info, nullptr, &swapchain_depth_image_view));
	}

	// Make framebuffers for each swapchain image:
	swapchain_framebuffers.assign(swapchain.image_views.size(), VK_NULL_HANDLE);
	for (size_t i = 0; i < swapchain.image_views.size(); ++i)
	{
		std::array<VkImageView, 2> attachments{
			swapchain.image_views[i],
			swapchain_depth_image_view,
		};
		VkFramebufferCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = render_pass,
			.attachmentCount = uint32_t(attachments.size()),
			.pAttachments = attachments.data(),
			.width = swapchain.extent.width,
			.height = swapchain.extent.height,
			.layers = 1,
		};

		VK(vkCreateFramebuffer(rtg.device, &create_info, nullptr, &swapchain_framebuffers[i]));
	}

	if (rtg.configuration.debug)
	{
		printf("swapchain images #: %zd\n", swapchain.image_views.size());
		std::string depth_info = string_VkFormat(depth_format);
		printf("depth-format: %s\n", depth_info.c_str());
	}
}

void Tutorial::destroy_framebuffers()
{
	for (VkFramebuffer &framebuffer : swapchain_framebuffers)
	{
		assert(framebuffer != VK_NULL_HANDLE);
		vkDestroyFramebuffer(rtg.device, framebuffer, nullptr);
		framebuffer = VK_NULL_HANDLE;
	}
	swapchain_framebuffers.clear();

	assert(swapchain_depth_image_view != VK_NULL_HANDLE);
	vkDestroyImageView(rtg.device, swapchain_depth_image_view, nullptr);
	swapchain_depth_image_view = VK_NULL_HANDLE;

	rtg.helpers.destroy_image(std::move(swapchain_depth_image));
}

void Tutorial::render(RTG &rtg_, RTG::RenderParams const &render_params)
{
	end = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> elapsed = (end - start) * 1000;
	start = end;
	// std::cout << "render  REPORT " << elapsed.count() << " ms" << std::endl;

	// assert that parameters are valid:
	assert(&rtg == &rtg_);
	assert(render_params.workspace_index < workspaces.size());
	if (!rtg.configuration.headless)
	{
		assert(render_params.image_index < swapchain_framebuffers.size());
	}

	// get more convenient names for the current workspace and target framebuffer:
	Workspace &workspace = workspaces[render_params.workspace_index];
	[[maybe_unused]] VkFramebuffer framebuffer = nullptr;

	if (!rtg.configuration.headless)
		framebuffer = swapchain_framebuffers[render_params.image_index];
	else
	{
		// TODO : set up the correct image
		VkFramebufferCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = render_pass,
			.attachmentCount = 2,
			.pAttachments = texture_views.data(),
			.width = textures[0].extent.width,
			.height = textures[0].extent.height,
			.layers = 1,
		};
		assert(texture_views.size() >= 2);
		VK(vkCreateFramebuffer(rtg.device, &create_info, nullptr, &framebuffer));
	}

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

	if (!rtg.configuration.headless)
	{
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

		{ // upload scene world info:
			assert(workspace.Scene_world_src.size == sizeof(world));

			// host-side copy into World_src:
			memcpy(workspace.Scene_world_src.allocation.data(), &world, sizeof(world));

			// add device-side copy from World_src -> World:
			assert(workspace.Scene_world_src.size == workspace.Scene_world.size);
			VkBufferCopy copy_region{
				.srcOffset = 0,
				.dstOffset = 0,
				.size = workspace.Scene_world_src.size,
			};
			vkCmdCopyBuffer(workspace.command_buffer, workspace.Scene_world_src.handle, workspace.Scene_world.handle, 1, &copy_region);
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
	}
	if (!scene_instances.empty())
	{ // upload scene transforms:
		//[re-]allocate lines buffers if needed:
		size_t needed_bytes = scene_instances.size() * sizeof(ScenesPipeline::Transform);
		if (workspace.Scene_transforms_src.handle == VK_NULL_HANDLE || workspace.Scene_transforms_src.size < needed_bytes)
		{
			// round to next multiple of 4k to avoid re-allocating continuously if vertex count grows slowly:
			size_t new_bytes = ((needed_bytes + 4096) / 4096) * 4096;

			if (workspace.Scene_transforms_src.handle)
			{
				rtg.helpers.destroy_buffer(std::move(workspace.Scene_transforms_src));
			}
			if (workspace.Scene_transforms.handle)
			{
				rtg.helpers.destroy_buffer(std::move(workspace.Scene_transforms));
			}

			workspace.Scene_transforms_src = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,											// going to have GPU copy from this memory
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // host-visible memory, coherent (no special sync needed)
				Helpers::Mapped																// get a pointer to the memory
			);
			workspace.Scene_transforms = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, // going to use as vertex buffer, also going to have GPU into this memory
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,								   // GPU-local memory
				Helpers::Unmapped													   // don't get a pointer to the memory
			);

			// update the descriptor set:
			VkDescriptorBufferInfo Transforms_info{
				.buffer = workspace.Scene_transforms.handle,
				.offset = 0,
				.range = workspace.Scene_transforms.size,
			};

			std::array<VkWriteDescriptorSet, 1> writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Scene_transforms_descriptors,
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

			std::cout << "Re-allocated scene transforms buffers to " << new_bytes << " bytes." << std::endl;
		}

		assert(workspace.Scene_transforms_src.size == workspace.Scene_transforms.size);
		assert(workspace.Scene_transforms_src.size >= needed_bytes);

		{ // copy transforms into Transforms_src:
			assert(workspace.Scene_transforms_src.allocation.mapped);
			ScenesPipeline::Transform *out = reinterpret_cast<ScenesPipeline::Transform *>(workspace.Scene_transforms_src.allocation.data()); // Strict aliasing violation, but it doesn't matter
			for (ScenesObjectInstance const &inst : scene_instances)
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
		vkCmdCopyBuffer(workspace.command_buffer, workspace.Scene_transforms_src.handle, workspace.Scene_transforms.handle, 1, &copy_region);
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
			VkClearValue{.color{.float32{0.f, 0.f, 0.f, 1.0f}}},
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

		{
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
		}
		// {
		// 	// draw with the background pipeline:
		// 	vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, background_pipeline.handle);

		// 	{ // push time:
		// 		BackgroundPipeline::Push push{
		// 			.time = float(time),
		// 		};
		// 		vkCmdPushConstants(workspace.command_buffer, background_pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
		// 	}

		// 	vkCmdDraw(workspace.command_buffer, 3, 1, 0, 0);
		// }

		// { // draw with the lines pipeline:
		// 	vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lines_pipeline.handle);

		// 	{ // use lines_vertices (offset 0) as vertex buffer binding 0:
		// 		std::array<VkBuffer, 1> vertex_buffers{workspace.lines_vertices.handle};
		// 		std::array<VkDeviceSize, 1> offsets{0};
		// 		vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
		// 	}

		// 	{ // bind Camera descriptor set:
		// 		std::array<VkDescriptorSet, 1> descriptor_sets{
		// 			workspace.Camera_descriptors, // 0: Camera
		// 		};
		// 		vkCmdBindDescriptorSets(
		// 			workspace.command_buffer,								  // command buffer
		// 			VK_PIPELINE_BIND_POINT_GRAPHICS,						  // pipeline bind point
		// 			lines_pipeline.layout,									  // pipeline layout
		// 			0,														  // first set
		// 			uint32_t(descriptor_sets.size()), descriptor_sets.data(), // descriptor sets count, ptr
		// 			0, nullptr												  // dynamic offsets count, ptr
		// 		);
		// 	}

		// 	// draw lines vertices:
		// 	vkCmdDraw(workspace.command_buffer, uint32_t(lines_vertices.size()), 1, 0, 0);
		// }

		// if (0)
		if (!object_instances.empty())
		{ // draw with the objects pipeline:
			std::cout << "object_instances.size(): " << object_instances.size() << "\n";
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
		}

		// if (0)
		if (!scene_instances.empty())
		{ // draw with the scene pipeline:
			// std::cout << "scene_instances #: " << scene_instances.size() << "\n";

			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scenes_pipeline.handle);

			{ // use object_vertices (offset 0) as vertex buffer binding 0:
				std::array<VkBuffer, 1> vertex_buffers{scene_vertices.handle};
				std::array<VkDeviceSize, 1> offsets{0};
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
			}

			{ // bind World and Transforms descriptor sets:
				std::array<VkDescriptorSet, 2> descriptor_sets{
					workspace.Scene_world_descriptors,		// 0: World
					workspace.Scene_transforms_descriptors, // 1: Transforms
				};
				vkCmdBindDescriptorSets(
					workspace.command_buffer,								  // command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS,						  // pipeline bind point
					scenes_pipeline.layout,									  // pipeline layout
					0,														  // first set
					uint32_t(descriptor_sets.size()), descriptor_sets.data(), // descriptor sets count, ptr
					0, nullptr												  // dynamic offsets count, ptr
				);
			}

			// Camera descriptor set is still bound, but unused(!)

			// draw all instances:
			for (ScenesObjectInstance const &inst : scene_instances)
			{
				uint32_t index = uint32_t(&inst - &scene_instances[0]);

				// bind texture descriptor set:
				vkCmdBindDescriptorSets(
					workspace.command_buffer,			   // command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS,	   // pipeline bind point
					scenes_pipeline.layout,				   // pipeline layout
					2,									   // second set
					1, &texture_descriptors[inst.texture], // descriptor sets count, ptr
					0, nullptr							   // dynamic offsets count, ptr
				);
				// std::cout << "ObjectInstance index: " << index << ", vertices count: " << inst.vertices.count << "\n";
				vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
			}
		}

		vkCmdEndRenderPass(workspace.command_buffer);
	}

	// end recording:
	VK(vkEndCommandBuffer(workspace.command_buffer));

	{ // submit `workspace.command buffer` for the GPU to run:
		std::array<VkSemaphore, 1> wait_semaphores{
			render_params.image_available};
		std::array<VkPipelineStageFlags, 1> wait_stages{
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
		static_assert(wait_semaphores.size() == wait_stages.size(), "every semaphore needs a stage");

		std::array<VkSemaphore, 1> signal_semaphores{
			render_params.image_done};
		VkSubmitInfo submit_info{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = uint32_t(wait_semaphores.size()),
			.pWaitSemaphores = wait_semaphores.data(),
			.pWaitDstStageMask = wait_stages.data(),
			.commandBufferCount = 1,
			.pCommandBuffers = &workspace.command_buffer,
			.signalSemaphoreCount = uint32_t(signal_semaphores.size()),
			.pSignalSemaphores = signal_semaphores.data(),
		};

		VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, render_params.workspace_available));
	}
}

void Tutorial::update(float dt)
{
	if (rtg.configuration.headless)
	{
		end = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end.time_since_epoch());
		std::cout << "call update " << duration.count() << "ms\n";
		// return;
	}

	time = std::fmod(playmode.time + dt, s72_scene.animation_duration);

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
							  0.0f, 0.2f, 0.5f * std::sin(ang),									// target
							  // 0.0f, 0.2f, 0.5f,
							  0.f, .5f, 0.5f // up
						  );
	}

	{
		// Define time-based variable for smooth transitions
		// float t = std::fmod(time, 60.0f) / 20.0f;

		// Use time to create rainbow-like colors (HSV to RGB conversion could also be used)
		// float red = 0.5f + 0.5f * std::sin(t * 2.0f * 3.14f + 0.0f);   // Red
		// float green = 0.5f + 0.5f * std::sin(t * 2.0f * 3.14f + 2.0f); // Green
		// float blue = 0.5f + 0.5f * std::sin(t * 2.0f * 3.14f + 4.0f);  // Blue
		float red = 1.f;   // Red
		float green = 1.f; // Green
		float blue = 1.f;  // Blue

		// Static sky direction (for now, you can rotate or move it to simulate dynamic weather)
		world.SKY_DIRECTION.x = 0.0f;
		world.SKY_DIRECTION.y = 0.0f;
		world.SKY_DIRECTION.z = 1.0f;

		// Set environment light color (dynamic rainbow effect)
		world.SKY_ENERGY.r = red;	// Red channel
		world.SKY_ENERGY.g = green; // Green channel
		world.SKY_ENERGY.b = blue;	// Blue channel

		// Optionally adjust intensity or distribution across the sky:
		float intensity = 0.8f; // Flicker intensity like sunlight through clouds

		// Modify sun light to be less dominant and emphasize rainbow light
		world.SUN_DIRECTION.x = 6.0f / 23.0f;
		world.SUN_DIRECTION.y = 13.0f / 23.0f;
		world.SUN_DIRECTION.z = 18.0f / 23.0f;

		world.SUN_ENERGY.r = intensity * 0.3f;
		world.SUN_ENERGY.g = intensity * 0.3f;
		world.SUN_ENERGY.b = intensity * 0.3f;
	}

	if (!rtg.configuration.headless)
	{
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

				mat4 ROTATE_X{
					1.0f, 0.0f, 0.0f, 0.0f,
					0.0f, cx, -sx, 0.0f,
					0.0f, sx, cx, 0.0f,
					0.0f, 0.0f, 0.0f, 1.0f};

				// Combine the rotations (apply in the desired order)
				mat4 WORLD_FROM_LOCAL_0 = ROTATE_X * WORLD_FROM_LOCAL; // Rotation about X-axis

				// object_instances.emplace_back(ObjectInstance{
				// 	.vertices = torus_vertices,
				// 	.transform{
				// 		.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL_0,
				// 		.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL_0,
				// 		.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL_0,
				// 	},
				// 	.texture = 2,
				// });
			}
		}
	}

	{ // make scene objects:
		scene_instances.clear();
		{
			glm::mat4 WORLD_FROM_LOCAL(1.0f);
			glm::mat4 CLIP_FROM_WORLD_SCENE(1.0f);
			glm::mat4 WORLD_FROM_LOCAL_debug(1.0f);

			// update transforms
			// in PLAY Animation_Mode, multiple animation matrix
			if (playmode.animation_mode == PLAY && !s72_scene.drivers.empty())
			{
				playmode.time += dt;
				if (playmode.time > s72_scene.animation_duration)
				{
					playmode.time -= s72_scene.animation_duration;
				}
				// std::cout << "play: " << playmode.time << "\n";

				for (auto &driver : s72_scene.drivers)
				{
					std::string node_name = driver.refnode_name;
					Node *node_ = s72_scene.nodes_map[node_name];
					driver.make_animation(playmode.time);
					s72_scene.transforms[node_] = node_->make_local_to_world();

					node_->child_forward_kinematics_transforms(node_);
					//   WORLD_FROM_LOCAL *= ANIMATION_MATRIX;
				}
			}

			if (!s72_scene.cameras.empty())
			{
				if (s72_scene.current_camera_ == nullptr)
				{
					s72_scene.current_camera_ = &(s72_scene.cameras[0]);
				}

				// TODO: for this camera, calculate the WORLD_FROM_LOCAL from path
				std::string camera_name = s72_scene.current_camera_->name; // Assuming the first camera
				if (s72_scene.cameras_path.find(camera_name) != s72_scene.cameras_path.end())
				{
					float aspect = s72_scene.current_camera_->perspective.aspect;
					float vfov = s72_scene.current_camera_->perspective.vfov;
					float near = s72_scene.current_camera_->perspective.near;
					float far = s72_scene.current_camera_->perspective.far;

					Node *camera_node_ = s72_scene.nodes_map[camera_name];

					// in USER mode, change camera position
					if (playmode.camera_mode == USER)
					{
						move_camera(dt, camera_node_);
					}

					auto mat_perspective = mat4_perspective(vfov, aspect, near, far);

					CLIP_FROM_WORLD_SCENE = mat_perspective * glm::mat4(camera_node_->make_world_to_local());

					// std::cout << "make_world_to_local\n";
					// printMat4(WORLD_FROM_LOCAL);

					// std::cout << "\nmake_local_to_world\n";
					// printMat4(node_->make_local_to_world());

					//  Now WORLD_FROM_LOCAL contains the final transformation from the local space of the camera to world space
					//  std::cout << "Final WORLD_FROM_LOCAL for camera " << camera_name << " calculated." << std::endl;
				}
			}

			for (const auto &scene_object : scene_objects)
			{
				Node *node_ = scene_object.object_node_;

				glm::mat4 obj_transform = s72_scene.transforms[node_];
				// std::cout << "\nobject world from local\n";
				// printMat4(obj_transform);

				WORLD_FROM_LOCAL = obj_transform;

				// culling
				if (playmode.camera_mode == DEBUG || playmode.cull_mode == FRUSTUM) // (playmode.cull_mode == FRUSTUM)
				{

					auto mesh_ = node_->mesh_;
					BBox bbox_trans = s72_scene.mesh_bbox_map[mesh_].transform(s72_scene.transforms[node_]);
					auto planes = extract_planes(CLIP_FROM_WORLD_SCENE);

					if (bbox_trans.is_bbox_outside_frustum(planes) == true)
					{
						continue;
					}
				}

				ScenesObjectInstance obj{
					.vertices = scene_object.scene_object_vertices,
					.transform{
						// .CLIP_FROM_LOCAL = CLIP_FROM_WORLD_SCENE * WORLD_FROM_LOCAL,
						// .WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
						// .WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL, // Assuming normal matrix is the same for simplicity
						// .WORLD_FROM_LOCAL_TANGENT = WORLD_FROM_LOCAL,
					},
					.texture = 0, // Assign the appropriate texture ID if needed
				};

				// glm::mat4 obj_world_from_local = WORLD_FROM_LOCAL * glm::make_mat4(obj_transform.WORLD_FROM_LOCAL.data());

				std::memcpy(obj.transform.CLIP_FROM_LOCAL.data(), glm::value_ptr(CLIP_FROM_WORLD_SCENE * WORLD_FROM_LOCAL), sizeof(float) * 16);
				// std::memcpy(obj.transform.CLIP_FROM_LOCAL.data(), glm::value_ptr(CLIP_FROM_WORLD_SCENE * WORLD_FROM_LOCAL), sizeof(float) * 16);
				std::memcpy(obj.transform.WORLD_FROM_LOCAL.data(), glm::value_ptr(WORLD_FROM_LOCAL), sizeof(float) * 16);
				std::memcpy(obj.transform.WORLD_FROM_LOCAL_NORMAL.data(), glm::value_ptr(WORLD_FROM_LOCAL), sizeof(float) * 16);
				std::memcpy(obj.transform.WORLD_FROM_LOCAL_TANGENT.data(), glm::value_ptr(WORLD_FROM_LOCAL), sizeof(float) * 16);

				scene_instances.emplace_back(obj);
			}
		}
	}
}

void Tutorial::on_input(InputEvent const &evt)
{
	if (evt.type == InputEvent::KeyDown)
	{
		if (evt.key.key == GLFW_KEY_1)
		{
			if (playmode.camera_mode == SCENE)
			{
				auto it_camera = s72_scene.cameras.begin();
				for (; it_camera != s72_scene.cameras.end(); ++it_camera)
				{
					if (it_camera->name == s72_scene.current_camera_->name && it_camera != s72_scene.cameras.end() - 1)
					{
						s72_scene.current_camera_ = &(*(it_camera + 1));
						break;
					}
					else if (it_camera->name == s72_scene.current_camera_->name && it_camera == s72_scene.cameras.end() - 1)
					{
						s72_scene.current_camera_ = &(s72_scene.cameras[0]);
						break;
					}
				}
				std::cout << "Switch to another scene camera. Current camera name: " << s72_scene.current_camera_->name << "\n";
			}
			else
			{
				std::cout << "Set camera mode " << 1 << " : scene camera\n";
				playmode.camera_mode = SCENE;
			}
			return;
		}
		else if (evt.key.key == GLFW_KEY_2)
		{
			std::cout << "Set camera mode " << 2 << " : user camera\n";
			playmode.camera_mode = USER;
			return;
		}
		else if (evt.key.key == GLFW_KEY_3)
		{
			std::cout << "Set camera mode " << 3 << " : debug camera\n";
			playmode.camera_mode = DEBUG;
			return;
		}
		else if (evt.key.key == GLFW_KEY_SPACE)
		{
			if (playmode.animation_mode == PAUSE)
			{
				std::cout << "start play " << "\n";
				playmode.animation_mode = PLAY;
			}
			else
			{
				std::cout << "pause " << "\n";
				playmode.animation_mode = PAUSE;
			}
			return;
		}
		if (playmode.camera_mode == USER)
		{
			if (evt.key.key == GLFW_KEY_A)
			{
				// std::cout << "camera move left	 ";
				playmode.left.downs += 1;
				playmode.left.pressed = true;
				return;
			}
			else if (evt.key.key == GLFW_KEY_D)
			{
				// std::cout << "camera move right	 ";
				playmode.right.downs += 1;
				playmode.right.pressed = true;
				return;
			}
			else if (evt.key.key == GLFW_KEY_W)
			{
				// std::cout << "camera move up	 ";
				playmode.up.downs += 1;
				playmode.up.pressed = true;
				return;
			}
			else if (evt.key.key == GLFW_KEY_S)
			{
				// std::cout << "camera move down	 ";
				playmode.down.downs += 1;
				playmode.down.pressed = true;
				return;
			}
		}
	}
	else if (playmode.camera_mode == USER && evt.type == InputEvent::KeyUp)
	{
		if (evt.key.key == GLFW_KEY_A)
		{
			playmode.left.pressed = false;
			return;
		}
		else if (evt.key.key == GLFW_KEY_D)
		{
			playmode.right.pressed = false;
			return;
		}
		else if (evt.key.key == GLFW_KEY_W)
		{
			playmode.up.pressed = false;
			return;
		}
		else if (evt.key.key == GLFW_KEY_S)
		{
			playmode.down.pressed = false;
			return;
		}
	}
	else if (playmode.camera_mode == USER && evt.type == InputEvent::MouseMotion)
	{
		if (s72_scene.current_camera_ != nullptr)
		{
			int width, height;
			glfwGetWindowSize(rtg.window, &width, &height);
			float rotation_coefficient = 0.5f;
			float delta_x = evt.motion.x - playmode.mouse_state.last_x;
			float delta_y = evt.motion.y - playmode.mouse_state.last_y;
			glm::vec2 motion = glm::vec2(rotation_coefficient * delta_x / float(width),
										 rotation_coefficient * (-delta_y) / float(height));

			// std::cout << "mouse move: " << motion.x << ", " << motion.y << "    ";

			[[maybe_unused]] Node *node_ = s72_scene.nodes_map[s72_scene.current_camera_->name];

			node_->rotation = glm::normalize(
				node_->rotation * glm::angleAxis(-motion.x * s72_scene.current_camera_->perspective.vfov, glm::vec3(0.0f, 1.0f, 0.0f)) *
				glm::angleAxis(motion.y * s72_scene.current_camera_->perspective.vfov, glm::vec3(1.0f, 0.0f, 0.0f)));

			// update mouse cor
			// Define the edge threshold
			int edge_threshold = 10;		   // Adjust this value based on your needs
			float edge_rotation_speed = 0.01f; // Adjust this value to control how fast the camera rotates at the edge

			// Check if the mouse is near the edge of the screen and apply additional rotation
			if (evt.motion.x <= edge_threshold)
			{
				// Mouse is near the left edge, rotate the camera left
				node_->rotation = glm::normalize(
					node_->rotation * glm::angleAxis(edge_rotation_speed * s72_scene.current_camera_->perspective.vfov, glm::vec3(0.0f, 1.0f, 0.0f)));
			}
			else if (evt.motion.x >= width - edge_threshold)
			{
				// Mouse is near the right edge, rotate the camera right
				node_->rotation = glm::normalize(
					node_->rotation * glm::angleAxis(-edge_rotation_speed * s72_scene.current_camera_->perspective.vfov, glm::vec3(0.0f, 1.0f, 0.0f)));
			}

			if (evt.motion.y <= edge_threshold)
			{
				// Mouse is near the top edge, rotate the camera up
				node_->rotation = glm::normalize(
					node_->rotation * glm::angleAxis(edge_rotation_speed * s72_scene.current_camera_->perspective.vfov, glm::vec3(1.0f, 0.0f, 0.0f)));
			}
			else if (evt.motion.y >= height - edge_threshold)
			{
				// Mouse is near the bottom edge, rotate the camera down
				node_->rotation = glm::normalize(
					node_->rotation * glm::angleAxis(-edge_rotation_speed * s72_scene.current_camera_->perspective.vfov, glm::vec3(1.0f, 0.0f, 0.0f)));
			}

			// Update mouse coordinates
			playmode.mouse_state.last_x = evt.motion.x;
			playmode.mouse_state.last_y = evt.motion.y;

			return;
		}
	}
}

void Tutorial::move_camera(float elapsed, Node *node_)
{
	// move camera:
	{
		// combine inputs into a move:
		constexpr float PlayerSpeed = 5.f;
		glm::vec2 move = glm::vec2(0.0f);
		if (playmode.left.pressed && !playmode.right.pressed)
			move.x = -1.0f;
		if (!playmode.left.pressed && playmode.right.pressed)
			move.x = 1.0f;
		if (playmode.down.pressed && !playmode.up.pressed)
			move.y = -1.0f;
		if (!playmode.down.pressed && playmode.up.pressed)
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
	playmode.left.downs = 0;
	playmode.right.downs = 0;
	playmode.up.downs = 0;
	playmode.down.downs = 0;
}

void Tutorial::load_s72()
{
	std::cout << std::filesystem::current_path() << "  load s72 file: ";
	std::string s72_file;
	s72_file = "./resource/" + rtg.configuration.scene_name;
	if (rtg.configuration.headless)
	{
		s72_file = "./resource/sphereflake.s72"; // set default
	}
	std::cout << s72_file;
	sejp::value val = sejp::load(s72_file);
	scene_workflow(val);
	// std::map<std::string, sejp::value> const &object = val.as_object().value();
}

void Tutorial::set_mesh_vertices_map(std::vector<SceneVertex> &vertices)
{
	for (auto &mesh : s72_scene.meshes)
	{
		// Get the source file from the POSITION attribute
		const std::string b72_file_path = "./resource/" + mesh.attributes.at("POSITION").src;

		const uint32_t stride = mesh.attributes.at("POSITION").stride;

		// Open the .b72 file
		std::ifstream file(b72_file_path, std::ios::binary);
		if (!file.is_open())
		{
			std::cerr << "Failed to open file: " << b72_file_path << std::endl;
			continue;
		}

		bool has_color = mesh.attributes.find("COLOR") != mesh.attributes.end();

		// Create ObjectVertices and bbox for this mesh
		MsehVertices mesh_vertices;
		BBox bbox;

		mesh_vertices.first = static_cast<uint32_t>(vertices.size());

		// Read each vertex by its stride
		for (uint32_t i = 0; i < mesh.count; ++i)
		{
			SceneVertex vertex;

			// Read a stride worth of data into a buffer
			std::vector<char> buffer(stride);
			file.read(buffer.data(), stride);

			// Extract Position
			const auto &pos_attr = mesh.attributes.at("POSITION");
			std::memcpy(&vertex.Position, buffer.data() + pos_attr.offset, sizeof(vertex.Position));

			// include vertex in bbox
			bbox.enclose(glm::vec3(vertex.Position.x, vertex.Position.y, vertex.Position.z));

			// Extract Normal
			const auto &normal_attr = mesh.attributes.at("NORMAL");
			std::memcpy(&vertex.Normal, buffer.data() + normal_attr.offset, sizeof(vertex.Normal));

			// Extract Tangent
			const auto &tangent_attr = mesh.attributes.at("TANGENT");
			std::memcpy(&vertex.Tangent, buffer.data() + tangent_attr.offset, sizeof(vertex.Tangent));

			// Extract TexCoord
			const auto &texcoord_attr = mesh.attributes.at("TEXCOORD");
			std::memcpy(&vertex.TexCoord, buffer.data() + texcoord_attr.offset, sizeof(vertex.TexCoord));

			// Extract Color if present
			if (has_color)
			{
				glm::u8vec4 color;
				const auto &color_attr = mesh.attributes.at("COLOR");
				std::memcpy(&color, buffer.data() + color_attr.offset, sizeof(color));
				vertex.color = std::optional<Color>{{color.r, color.g, color.b, color.a}};
			}

			// Push the vertex into the vertices vector
			vertices.push_back(vertex);
		}
		// save in global
		s72_scene.mesh_bbox_map[&mesh] = bbox;

		// Set vertex count in object_vertices
		mesh_vertices.count = mesh.count;

		s72_scene.mesh_vertices_map[&mesh] = mesh_vertices;

		// std::cout << "Last vertex coordinates: " << vertices.back().Position.x << ", "
		//		  << vertices.back().Position.y << ", " << vertices.back().Position.z << "\n";

		file.close();
	}
}

// dfs order
void Tutorial::process_node(std::vector<SceneVertex> &vertices, Node *node)
{
	if (!node || !node->mesh_)
		return;

	Mesh *mesh_ = node->mesh_;

	// Create ObjectVertices for this mesh
	auto obj_vertices = s72_scene.mesh_vertices_map[mesh_];

	SceneObject scene_object;
	std::memcpy(&(scene_object.scene_object_vertices), &obj_vertices, sizeof(obj_vertices));

	// scene_object.scene_object_vertices = obj_vertices;

	// Push the ObjectVertices to the scene_object_vertices vector
	// scene_object_vertices.push_back(obj_vertices);

	// Push the transform matrix to scene_transform
	glm::vec4 extra_column = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	glm::mat4 combined_matrix = glm::mat4(
		glm::vec4(node->make_local_to_world()[0], extra_column[0]),
		glm::vec4(node->make_local_to_world()[1], extra_column[1]),
		glm::vec4(node->make_local_to_world()[2], extra_column[2]),
		glm::vec4(node->make_local_to_world()[3], extra_column[3]));
	// scene_transform.push_back(combined_matrix);

	scene_object.scene_transform = combined_matrix;
	scene_object.object_node_ = node;
	scene_objects.push_back(scene_object);

	// save in global
	s72_scene.transforms[node] = combined_matrix;
}

void Tutorial::load_vertex_from_b72(std::vector<SceneVertex> &vertices)
{
	// std::cout << "load_vertex_from_b72\n";
	set_mesh_vertices_map(vertices);

	// DFS function to traverse and process vertices for each node
	std::function<void(Node *)> dfs_process_node = [&](Node *node)
	{
		if (!node)
			return;

		// Process node if it has a mesh
		process_node(vertices, node);

		// Traverse the children nodes
		for (const auto &child_variant : node->children)
		{
			Node *child_node = find_node_by_name_or_index(child_variant);
			if (child_node)
			{
				dfs_process_node(child_node);
			}
		}
	};

	// Start DFS from each root node in the scene
	for (const auto &[key, root_node] : s72_scene.nodes_map)
	{
		dfs_process_node(root_node);
	}
	// std::cout << "load_vertex_from_b72 done\n";
}
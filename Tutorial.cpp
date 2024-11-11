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
#include "lib/Frustum.h"

#include "include/stb/stb_image.h"

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
	create_render_pass();
	create_shadow_renderpass();

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

	// create pipelines
	//  background_pipeline.create(rtg, render_pass, 0);
	lines_pipeline.create(rtg, render_pass, 0);
	objects_pipeline.create(rtg, render_pass, 0);
	scenes_pipeline.create(rtg, render_pass, 0);
	shadow_pipeline.create(rtg, shadow_map_render_pass, 0);

	// create descriptor pool:
	create_description_pool();

	setup_workspaces();

	{
		// create scene object vertices
		std::vector<SceneVertex> vertices;

		set_mesh_vertices_map(vertices);

		set_mesh_material_map();

		set_scene_objects(vertices);
		// load_vertex_from_b72(vertices);
		// print_s72();

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

	{
		// environment texture
		if (s72_scene.environment.name != "")
		{
			setup_environ();
		}
		else
			make_default_environ();

		setup_env_views_sample();
	}

	{ // make some textures
		textures.reserve(2);

		make_default_texture();

		load_scene_object_textures();

		setup_material_textureindex_map();

		setup_views_sample();

		make_default_normal();
		setup_normal_views_sample();

		make_default_disp();
		setup_disp_views_sample();
		printf("make some textures...done\n");
	}

	setup_texture_descriptor_pool();

	make_texture_descriptor_sets();
	printf("make_texture_descriptor_sets...done\n");

	{ // shadow map
		setup_shadow_image();
		setup_shadow_views_sample();
		create_shadow_framebuffer();
	}

	{ // terrain
		if (s72_scene.terrain.name != "")
		{
			prepare_terrain();
		}
	}

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

	if (shadow_map_framebuffer != VK_NULL_HANDLE)
	{
		vkDestroyFramebuffer(rtg.device, shadow_map_framebuffer, nullptr);
	}

	if (shadow_sampler)
	{
		vkDestroySampler(rtg.device, shadow_sampler, nullptr);
		shadow_sampler = VK_NULL_HANDLE;
	}

	if (shadow_view)
	{
		vkDestroyImageView(rtg.device, shadow_view, nullptr);
		shadow_view = VK_NULL_HANDLE;
	}

	if (shadow_map.handle)
	{
		rtg.helpers.destroy_image(std::move(shadow_map));
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

	if (Scene_env_sampler)
	{
		vkDestroySampler(rtg.device, Scene_env_sampler, nullptr);
		Scene_env_sampler = VK_NULL_HANDLE;
	}

	if (Scene_env_view)
	{
		vkDestroyImageView(rtg.device, Scene_env_view, nullptr);
		Scene_env_view = VK_NULL_HANDLE;
	}

	if (Scene_env.handle)
	{
		rtg.helpers.destroy_image(std::move(Scene_env));
	}

	if (Lamber_env_view)
	{
		vkDestroyImageView(rtg.device, Lamber_env_view, nullptr);
		Lamber_env_view = VK_NULL_HANDLE;
	}

	if (Lamber_env.handle)
	{
		rtg.helpers.destroy_image(std::move(Lamber_env));
	}

	if (disp_sampler)
	{
		vkDestroySampler(rtg.device, disp_sampler, nullptr);
		disp_sampler = VK_NULL_HANDLE;
	}

	if (flat_disp_view)
	{
		vkDestroyImageView(rtg.device, flat_disp_view, nullptr);
		flat_disp_view = VK_NULL_HANDLE;
	}

	if (flat_disp_map.handle)
	{
		rtg.helpers.destroy_image(std::move(flat_disp_map));
	}

	if (normal_sampler)
	{
		vkDestroySampler(rtg.device, normal_sampler, nullptr);
		normal_sampler = VK_NULL_HANDLE;
	}

	if (flat_normal_view)
	{
		vkDestroyImageView(rtg.device, flat_normal_view, nullptr);
		flat_normal_view = VK_NULL_HANDLE;
	}

	if (flat_normal_map.handle)
	{
		rtg.helpers.destroy_image(std::move(flat_normal_map));
	}

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
	shadow_pipeline.destroy(rtg);

	for (Workspace &workspace : workspaces)
	{
		if (workspace.command_buffer != VK_NULL_HANDLE)
		{
			vkFreeCommandBuffers(rtg.device, command_pool, 1, &workspace.command_buffer);
			workspace.command_buffer = VK_NULL_HANDLE;
		}

		if (workspace.shadow_map_cmd_buf != VK_NULL_HANDLE)
		{
			vkFreeCommandBuffers(rtg.device, command_pool, 1, &workspace.shadow_map_cmd_buf);
			workspace.shadow_map_cmd_buf = VK_NULL_HANDLE;
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
		if (workspace.Scene_light_src.handle != VK_NULL_HANDLE)
		{
			rtg.helpers.destroy_buffer(std::move(workspace.Scene_light_src));
		}
		if (workspace.Scene_light.handle != VK_NULL_HANDLE)
		{
			rtg.helpers.destroy_buffer(std::move(workspace.Scene_light));
		}

		if (workspace.Shadow_view_projection_src.handle != VK_NULL_HANDLE)
		{
			rtg.helpers.destroy_buffer(std::move(workspace.Shadow_view_projection_src));
		}
		if (workspace.Shadow_view_projection.handle != VK_NULL_HANDLE)
		{
			rtg.helpers.destroy_buffer(std::move(workspace.Shadow_view_projection));
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

	if (shadow_map_render_pass != VK_NULL_HANDLE)
	{
		vkDestroyRenderPass(rtg.device, shadow_map_render_pass, nullptr);
		shadow_map_render_pass = VK_NULL_HANDLE;
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
	// end = std::chrono::high_resolution_clock::now();
	// std::chrono::duration<double> elapsed = (end - start) * 1000;
	// start = end;
	// std::cout << "render  REPORT " << elapsed.count() << " ms" << std::endl;

	// assert that parameters are valid:
	assert(&rtg == &rtg_);
	assert(render_params.workspace_index < workspaces.size());
	if (!rtg.configuration.headless)
	{
		assert(render_params.image_index < swapchain_framebuffers.size());
	}

	shadow_render(render_params);
	vkQueueWaitIdle(rtg.graphics_queue);

	// get more convenient names for the current workspace and target framebuffer:
	Workspace &workspace = workspaces[render_params.workspace_index];
	[[maybe_unused]] VkFramebuffer framebuffer = nullptr;

	framebuffer = swapchain_framebuffers[render_params.image_index];

	// record (into `workspace.command_buffer`) commands that run a `render_pass` that just clears `framebuffer`:
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

	transitionImageLayout(workspace.command_buffer, shadow_map.handle, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

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

	render_upload_scene_instances(render_params);

	render_upload_lights(render_params);

	render_pass_command(render_params, framebuffer);

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

void Tutorial::render_upload_scene_instances(RTG::RenderParams const &render_params)
{
	Workspace &workspace = workspaces[render_params.workspace_index];
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
}

void Tutorial::render_upload_lights(RTG::RenderParams const &render_params)
{
	Workspace &workspace = workspaces[render_params.workspace_index];
	if (!s72_scene.lights.empty())
	{
		size_t needed_bytes = total_light_obj_cnt * sizeof(ScenesPipeline::Light);
		if (workspace.Scene_light_src.handle == VK_NULL_HANDLE || workspace.Scene_light_src.size < needed_bytes)
		{
			size_t new_bytes = ((needed_bytes + 192) / 192) * 192;
			if (workspace.Scene_light_src.handle)
			{
				rtg.helpers.destroy_buffer(std::move(workspace.Scene_light_src));
			}
			if (workspace.Scene_light.handle)
			{
				rtg.helpers.destroy_buffer(std::move(workspace.Scene_light));
			}

			workspace.Scene_light_src = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,											// going to have GPU copy from this memory
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // host-visible memory, coherent (no special sync needed)
				Helpers::Mapped																// get a pointer to the memory
			);
			workspace.Scene_light = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, // going to use as vertex buffer, also going to have GPU into this memory
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,								   // GPU-local memory
				Helpers::Unmapped													   // don't get a pointer to the memory
			);

			// update the descriptor set:
			VkDescriptorBufferInfo Lights_info{
				.buffer = workspace.Scene_light.handle,
				.offset = 0,
				.range = workspace.Scene_light.size,
			};

			VkDescriptorImageInfo shadow_map_info{};
			shadow_map_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			shadow_map_info.imageView = shadow_view;  // View of the shadow map
			shadow_map_info.sampler = shadow_sampler; // Sampler created for shadow map

			std::array<VkWriteDescriptorSet, 2> writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Scene_light_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &Lights_info,
				},
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Scene_light_descriptors,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = &shadow_map_info,
				},
			};

			vkUpdateDescriptorSets(
				rtg.device,
				uint32_t(writes.size()), writes.data(), // descriptorWrites count, data
				0, nullptr								// descriptorCopies count, data
			);

			std::cout << "Re-allocated scene light buffers to " << new_bytes << " bytes." << std::endl;
		}
		assert(workspace.Scene_light_src.size == workspace.Scene_light.size);
		assert(workspace.Scene_light_src.size >= needed_bytes);

		// Map the host-visible buffer to copy data
		assert(workspace.Scene_light_src.allocation.mapped);																 // Ensure mapped pointer is available
		ScenesPipeline::Light *out = reinterpret_cast<ScenesPipeline::Light *>(workspace.Scene_light_src.allocation.data()); // Map buffer

		// Copy the lights data to the mapped memory
		for (LightObject &light : s72_scene.lights)
		{
			if (s72_scene.light_node_map.find(&light) != s72_scene.light_node_map.end())
			{
				auto nodes = s72_scene.light_node_map[&light];
				for (auto node_ : nodes)
				{
					// Node *node_ = s72_scene.light_node_map[&light];
					glm::mat4 mat_perspective(1.0f);
					auto transform = s72_scene.transforms[node_];

					out->light_obj.tint = light.tint;
					out->light_obj.type = light.type;
					// printf("Type: %d ", out->light_obj.type);

					if (out->light_obj.type == SUN)
					{
						out->light_obj.data.sun = light.data.sun;
					}
					else if (out->light_obj.type == SPHERE)
					{
						out->light_obj.data.sphere = light.data.sphere;
					}
					else if (out->light_obj.type == SPOT)
					{
						out->light_obj.data.spot = light.data.spot;
						float far = light.data.spot.limit > 0.1f ? light.data.spot.limit : 1e5f;
						mat_perspective = mat4_perspective(light.data.spot.fov, 1.f, 0.1f, far);
					}

					out->light_obj.shadow = light.shadow;

					out->position = transform[3];
					out->quaternion = extract_rotation_quaternion(transform);

					out->transform = mat_perspective * glm::mat4(node_->make_world_to_local());

					++out;
				}
			}
		}

		// device-side copy
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = needed_bytes,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.Scene_light_src.handle, workspace.Scene_light.handle, 1, &copy_region);
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
}

void Tutorial::set_shadow_viewport(RTG::RenderParams const &render_params, int row, int col)
{
	Workspace &workspace = workspaces[render_params.workspace_index];
	// configure viewport transform
	VkViewport viewport{
		.x = float(col * SHADOW_MAP_WIDTH),
		.y = float(row * SHADOW_MAP_HEIGHT),
		.width = float(SHADOW_MAP_WIDTH),
		.height = float(SHADOW_MAP_HEIGHT),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	vkCmdSetViewport(workspace.shadow_map_cmd_buf, 0, 1, &viewport);

	// set scissor rectangle:
	VkRect2D scissor{
		.offset = {int32_t(viewport.x), int32_t(viewport.y)},
		.extent = {uint32_t(viewport.width), uint32_t(viewport.height)},
	};
	vkCmdSetScissor(workspace.shadow_map_cmd_buf, 0, 1, &scissor);
}

void Tutorial::transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
{
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; // For depth images
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

	if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		sourceStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}

	vkCmdPipelineBarrier(
		commandBuffer,
		sourceStage, destinationStage,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier);
}

void Tutorial::shadow_render(RTG::RenderParams const &render_params)
{
	Workspace &workspace = workspaces[render_params.workspace_index];
	{ // begin recording:
		VkCommandBufferBeginInfo begin_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			//.pNext set to nullptr by zero-initialization!
			.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT, // will record again every submit
		};
		VK(vkBeginCommandBuffer(workspace.shadow_map_cmd_buf, &begin_info));
	}

	VkClearValue clear_values[1];
	clear_values[0].depthStencil.depth = 1.0f;
	clear_values[0].depthStencil.stencil = 0;

	VkRenderPassBeginInfo rp_begin;
	rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rp_begin.pNext = NULL;
	rp_begin.renderPass = shadow_map_render_pass;
	rp_begin.framebuffer = shadow_map_framebuffer;
	rp_begin.renderArea.offset.x = 0;
	rp_begin.renderArea.offset.y = 0;
	rp_begin.renderArea.extent.width = SHADOW_MAP_WIDTH * shadow_map_grid_size;
	rp_begin.renderArea.extent.height = SHADOW_MAP_HEIGHT * shadow_map_grid_size;
	rp_begin.clearValueCount = 1;
	rp_begin.pClearValues = clear_values;

	vkCmdBeginRenderPass(workspace.shadow_map_cmd_buf,
						 &rp_begin,
						 VK_SUBPASS_CONTENTS_INLINE);

	if (!scene_instances.empty())
	{
		vkCmdBindPipeline(workspace.shadow_map_cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline.handle);

		{ // use object_vertices (offset 0) as vertex buffer binding 0:
			std::array<VkBuffer, 1> vertex_buffers{scene_vertices.handle};
			std::array<VkDeviceSize, 1> offsets{0};
			vkCmdBindVertexBuffers(workspace.shadow_map_cmd_buf, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());

			// bind VP and Transforms descriptor sets:
			std::array<VkDescriptorSet, 2> descriptor_sets{
				workspace.Shadow_view_projection_descriptors, // 0: VP
				workspace.Scene_transforms_descriptors,		  // 1: Transforms, this is shared with scene_transform
			};

			vkCmdBindDescriptorSets(workspace.shadow_map_cmd_buf,
									VK_PIPELINE_BIND_POINT_GRAPHICS,
									shadow_pipeline.layout,
									0, // first set
									uint32_t(descriptor_sets.size()), descriptor_sets.data(),
									0, nullptr);
		}

		int i = 0;
		for (LightObject &light : s72_scene.lights)
		{
			int row = i / shadow_map_grid_size;
			int col = i % shadow_map_grid_size;

			if (light.type == SPOT)
			{
				glm::mat4 transform = glm::mat4(1.0f);
				if (s72_scene.light_node_map.find(&light) != s72_scene.light_node_map.end())
				{
					auto nodes = s72_scene.light_node_map[&light];
					// Node *node_ = s72_scene.light_node_map[&light];
					for (auto node_ : nodes)
					{
						row = i / shadow_map_grid_size;
						col = i % shadow_map_grid_size;
						set_shadow_viewport(render_params, row, col);

						transform = glm::mat4(node_->make_world_to_local());

						float aspect = 1.0f;
						float vfov = light.data.spot.fov;
						float near = 0.1f;
						float far = light.data.spot.radius > 0.1f ? light.data.spot.radius : 1e8f;

						auto mat_perspective = mat4_perspective(vfov, aspect, near, far);

						ShadowPipeline::Push push{
							.CLIP_FORM_LIGHT = mat_perspective * transform,
						};

						vkCmdPushConstants(workspace.shadow_map_cmd_buf, shadow_pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

						// draw all instances:
						for (ScenesObjectInstance const &inst : scene_instances)
						{
							uint32_t index = uint32_t(&inst - &scene_instances[0]);

							vkCmdDraw(workspace.shadow_map_cmd_buf, inst.vertices.count, 1, inst.vertices.first, index);
						}
						i++;
					}
				}
			}
		}
	}

	vkCmdEndRenderPass(workspace.shadow_map_cmd_buf);

	// end recording:
	VK(vkEndCommandBuffer(workspace.shadow_map_cmd_buf));

	std::array<VkSemaphore, 1> signal_semaphores{
		render_params.shadow_image_done};
	// VkPipelineStageFlags shadow_map_wait_stages = 0;
	VkSubmitInfo submit_info = {};
	submit_info.pNext = NULL;
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.waitSemaphoreCount = 0;
	submit_info.pWaitSemaphores = NULL;
	submit_info.signalSemaphoreCount = 0;
	submit_info.pSignalSemaphores = nullptr; // signal_semaphores.data();
	submit_info.pWaitDstStageMask = 0;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &workspace.shadow_map_cmd_buf;

	VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, NULL));
}

void Tutorial::render_pass_command(RTG::RenderParams const &render_params, VkFramebuffer &framebuffer)
{
	Workspace &workspace = workspaces[render_params.workspace_index];
	// put GPU commands here!
	// render pass
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

			// set default be the last one descriptor set, set in void Toturial::make_texture_descriptor_sets();
			auto texture_descriptor_index = s72_scene.material_descriptor_index_map.size();

			if (s72_scene.material_descriptor_index_map.find(inst.material_) != s72_scene.material_descriptor_index_map.end())
				texture_descriptor_index = s72_scene.material_descriptor_index_map[inst.material_];

			// bind texture descriptor set:
			vkCmdBindDescriptorSets(
				workspace.command_buffer,						   // command buffer
				VK_PIPELINE_BIND_POINT_GRAPHICS,				   // pipeline bind point
				scenes_pipeline.layout,							   // pipeline layout
				2,												   // second set
				1, &texture_descriptors[texture_descriptor_index], // descriptor sets count, ptr
				0, nullptr										   // dynamic offsets count, ptr
			);

			// bind lights descriptor set:
			vkCmdBindDescriptorSets(
				workspace.command_buffer,			   // command buffer
				VK_PIPELINE_BIND_POINT_GRAPHICS,	   // pipeline bind point
				scenes_pipeline.layout,				   // pipeline layout
				3,									   // third set
				1, &workspace.Scene_light_descriptors, // descriptor sets count, ptr
				0, nullptr							   // dynamic offsets count, ptr
			);

			{ // push materialType:
				// std::cout << "lights_size: " << s72_scene.lights.size();

				ScenesPipeline::Push push{
					.materialType = inst.material_->type,
					.lights_size = (int)s72_scene.lights.size(),
				};

				if (s72_scene.environment.name != "")
				{
					push.src_env = 1;
				}
				else
					push.src_env = 0;

				if (inst.material_->type == LAMBERTIAN && std::holds_alternative<LambertianMaterial>(inst.material_->material))
				{
					auto lamber = std::get<LambertianMaterial>(inst.material_->material);
					if (std::holds_alternative<glm::vec3>(lamber.albedo))
					{
						glm::vec3 albedo = std::get<glm::vec3>(lamber.albedo);
						push.albedo.r = albedo.r;
						push.albedo.g = albedo.g;
						push.albedo.b = albedo.b;
					}
					else if (std::holds_alternative<Texture>(lamber.albedo))
					{
						push.src_albedo = 1; // png sign
					}
				}
				else if (inst.material_->type == PBR && std::holds_alternative<PBRMaterial>(inst.material_->material))
				{
					auto pbr = std::get<PBRMaterial>(inst.material_->material);
					if (std::holds_alternative<glm::vec3>(pbr.albedo))
					{
						glm::vec3 albedo = std::get<glm::vec3>(pbr.albedo);
						push.albedo.r = albedo.r;
						push.albedo.g = albedo.g;
						push.albedo.b = albedo.b;
					}
					else if (std::holds_alternative<Texture>(pbr.albedo))
					{
						push.src_albedo = 1; // png sign
					}
				}

				vkCmdPushConstants(workspace.command_buffer, scenes_pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);

				// std::cout << " done!" << std::endl;
			}

			// std::cout << "ObjectInstance index: " << index << ", vertices count: " << inst.vertices.count << "\n";
			vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
		}
	}

	vkCmdEndRenderPass(workspace.command_buffer);
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

		world.SUN_ENERGY.r = intensity;
		world.SUN_ENERGY.g = intensity;
		world.SUN_ENERGY.b = intensity;
	}

	{ // make scene objects:
		scene_instances.clear();
		{
			glm::mat4 WORLD_FROM_LOCAL(1.0f);
			glm::mat4 CLIP_FROM_WORLD_SCENE(1.0f);
			glm::mat4 WORLD_FROM_LOCAL_debug(1.0f);
			glm::mat4 CULL(1.0f);

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
					// s72_scene.transforms[node_] = node_->make_local_to_world();

					node_->child_forward_kinematics_transforms(node_);
					s72_scene.transforms[node_] = node_->make_local_to_world();
					//   WORLD_FROM_LOCAL *= ANIMATION_MATRIX;
				}
			}

			if (!s72_scene.cameras.empty())
			{
				if (s72_scene.current_camera_ == nullptr)
				{
					s72_scene.current_camera_ = &(s72_scene.cameras[0]);
				}

				// for this camera, calculate the WORLD_FROM_LOCAL from path
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

					CULL = mat_perspective;

					CLIP_FROM_WORLD_SCENE = mat_perspective * glm::mat4(camera_node_->make_world_to_local());

					glm::vec3 eye_world_position = glm::vec3(camera_node_->make_local_to_world() * glm::vec4(0.f, 0.f, 0.f, 1.0f));

					// update eye position
					world.EYE.x = eye_world_position.x;
					world.EYE.y = eye_world_position.y;
					world.EYE.z = eye_world_position.z;
				}
			}

			// std::cout << std::endl;

			for (const auto &scene_object : scene_objects)
			{
				Node *node_ = scene_object.object_node_;
				auto mesh_ = node_->mesh_;
				MaterialObject *material_obj_ = nullptr;
				if (auto it = s72_scene.mesh_material_map.find(mesh_); it != s72_scene.mesh_material_map.end())
				{
					material_obj_ = it->second;
				}
				else
				{ // select the default material
					material_obj_ = &(s72_scene.materials.back());
				}

				glm::mat4 obj_transform = s72_scene.transforms[node_];
				// std::cout << "\nobject world from local\n";
				// printMat4(obj_transform);

				WORLD_FROM_LOCAL = obj_transform;

				// culling
				if (playmode.camera_mode == DEBUG || playmode.cull_mode == FRUSTUM)
				{
					uint32_t index = uint32_t(&scene_object - &scene_objects[0]);

					assert(s72_scene.mesh_bbox_map.find(mesh_) != s72_scene.mesh_bbox_map.end());

					BBox bbox_trans = s72_scene.mesh_bbox_map[mesh_];

					printf("%s, index %d, min:%f, %f, %f, max:%f, %f, %f, bbox center %f, %f, %f, r: %f\n", node_->name.c_str(), index,
						   bbox_trans.min.x, bbox_trans.min.y, bbox_trans.min.z,
						   bbox_trans.max.x, bbox_trans.max.y, bbox_trans.max.z,
						   bbox_trans.center().x, bbox_trans.center().y, bbox_trans.center().z,
						   glm::distance(bbox_trans.max, bbox_trans.min) * 0.5f);

					bbox_trans = s72_scene.mesh_bbox_map[mesh_].transform(WORLD_FROM_LOCAL); // bug
					auto planes = extract_planes(CLIP_FROM_WORLD_SCENE);
					// auto vertices = extractFrustumVertices(CLIP_FROM_WORLD_SCENE);
					//  auto frustum = Frustum::createFrustumFromMatrix(CLIP_FROM_WORLD_SCENE);

					printf("%s, index %d, bbox center %f, %f, %f, r: %f\n", node_->name.c_str(), index,
						   bbox_trans.center().x, bbox_trans.center().y, bbox_trans.center().z,
						   glm::distance(bbox_trans.max, bbox_trans.min) * 0.5f);

					if (bbox_trans.is_bbox_outside_frustum_1(planes))
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
					.mesh_ = mesh_,
					.material_ = material_obj_,
				};

				if (material_obj_->type == LAMBERTIAN && std::holds_alternative<LambertianMaterial>(material_obj_->material))
				{
					auto lamber = std::get<LambertianMaterial>(material_obj_->material);
					if (std::holds_alternative<Texture>(lamber.albedo))
					{
						auto texture = std::get<Texture>(lamber.albedo);
						std::string src = texture.src;
						if (s72_scene.textures_src_index_map.find(src) != s72_scene.textures_src_index_map.end())
						{
							// printf("%s: %s\n", mesh_->name.c_str(), src.c_str());
							obj.texture = s72_scene.textures_src_index_map[src];
						}
					}
				}
				else if (material_obj_->type == PBR && std::holds_alternative<PBRMaterial>(material_obj_->material))
				{
					auto pbr = std::get<PBRMaterial>(material_obj_->material);
					if (std::holds_alternative<Texture>(pbr.albedo))
					{
						auto texture = std::get<Texture>(pbr.albedo);
						std::string src = texture.src;
						if (s72_scene.textures_src_index_map.find(src) != s72_scene.textures_src_index_map.end())
						{
							// printf("%s: %s\n", mesh_->name.c_str(), src.c_str());
							obj.texture = s72_scene.textures_src_index_map[src];
						}
					}
				}

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

void Tutorial::create_render_pass()
{
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
}

void Tutorial::create_description_pool()
{
	uint32_t per_workspace = uint32_t(rtg.workspaces.size()); // for easier-to-read counting

	std::array<VkDescriptorPoolSize, 2> pool_sizes{
		VkDescriptorPoolSize{
			// for camera
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 4 * per_workspace, // 4 descriptor per set, one set per workspace
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
		.maxSets = 8 * per_workspace, // three set per workspace
		.poolSizeCount = uint32_t(pool_sizes.size()),
		.pPoolSizes = pool_sizes.data(),
	};

	VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool));
}

void Tutorial::setup_workspaces()
{
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

		{ // allocate command buffer:
			VkCommandBufferAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool = command_pool,
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1,
			};
			VK(vkAllocateCommandBuffers(rtg.device, &alloc_info, &workspace.shadow_map_cmd_buf));
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

		{ // allocate descriptor set for lights descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &scenes_pipeline.set3_Light,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Scene_light_descriptors));
			// NOTE: will fill in this descriptor set in render when buffers are [re-]allocated
		}

		workspace.Shadow_view_projection_src = rtg.helpers.create_buffer(
			sizeof(ShadowPipeline::ViewProjection),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,											// going to have GPU copy from this memory
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // host-visible memory, coherent (no special sync needed)
			Helpers::Mapped																// get a pointer to the memory
		);
		workspace.Shadow_view_projection = rtg.helpers.create_buffer(
			sizeof(ShadowPipeline::ViewProjection),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, // going to use as a uniform buffer, also going to have GPU copy into this memory
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,								   // GPU-local memory
			Helpers::Unmapped													   // don't get a pointer to the memory
		);

		{ // allocate descriptor set for Scene_camera descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &shadow_pipeline.set0_ViewProjection,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Shadow_view_projection_descriptors));
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

			VkDescriptorBufferInfo Shadow_VP_info{
				.buffer = workspace.Shadow_view_projection.handle,
				.offset = 0,
				.range = workspace.Shadow_view_projection.size,
			};

			std::vector<VkWriteDescriptorSet> writes;

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

			writes.emplace_back(VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = workspace.Scene_world_descriptors,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pBufferInfo = &Scene_world_info,
			});

			writes.emplace_back(VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = workspace.Shadow_view_projection_descriptors,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pBufferInfo = &Shadow_VP_info,
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
}

void Tutorial::setup_environ()
{
	assert(s72_scene.environment.name != "");

	// read data from png
	int w, h, n, ok;
	std::string filename = "./resource/" + s72_scene.environment.radiance.src;
	[[maybe_unused]] unsigned char *image_data = stbi_load(filename.c_str(), &w, &h, &n, 0);
	ok = stbi_info(filename.c_str(), &w, &h, &n);
	std::cout << "environment: " << filename.c_str() << " ok? " << ok << ": " << w << ", " << h << ", " << n << "\n";

	int per_h = h / 6;
	// make up 6 faces
	/*std::vector<stbi_uc *> faces(6);
	for (int i = 0; i < 6; i++)
	{
		faces[i] = new stbi_uc[per_h * w * n];
		memcpy(faces[i], image_data + i * per_h * w * n, per_h * w * n);
	}*/

	// make up cubemap image
	std::cout << "create_cubemap_image...  ";
	Scene_env = rtg.helpers.create_cubemap_image(
		VkExtent2D{.width = (uint32_t)w, .height = (uint32_t)per_h}, // size of image
		VK_FORMAT_R8G8B8A8_UNORM,									 // how to interpret image data (in this case, linearly-encoded 8-bit RGBA)
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // should be device-local
		Helpers::Unmapped);
	std::cout << " success\n";

	// transfer data:
	rtg.helpers.transfer_to_cubemap_image(image_data, w * h * n, Scene_env); //

	stbi_image_free(image_data);

	// read lamber_env if png exist
	std::string &env_file = s72_scene.environment.radiance.src;
	std::string lamber_env = "./resource/" + env_file.substr(0, env_file.size() - 4) + ".lambertian.png";
	printf("lamber_env: %s\n", lamber_env.c_str());

	ok = stbi_info(lamber_env.c_str(), &w, &h, &n);
	if (ok != 1)
		return;

	image_data = stbi_load(lamber_env.c_str(), &w, &h, &n, 0);
	per_h = h / 6;

	Lamber_env = rtg.helpers.create_cubemap_image(
		VkExtent2D{.width = (uint32_t)w, .height = (uint32_t)per_h}, // size of image
		VK_FORMAT_R8G8B8A8_UNORM,									 // how to interpret image data (in this case, linearly-encoded 8-bit RGBA)
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // should be device-local
		Helpers::Unmapped);

	rtg.helpers.transfer_to_cubemap_image(image_data, w * h * n, Lamber_env);
	printf("make up lamber_env success: w=%d, h=%d, n=%d\n", w, h, n);
	stbi_image_free(image_data);
}

void Tutorial::make_default_environ()
{
	{ // texture 0 will be a dark grey / light grey checkerboard with a red square at the origin.
		// actually make the texture:
		uint32_t size = 1;

		std::vector<uint32_t> data;
		data.reserve(size * 6 * size);
		// for (uint32_t y = 0; y < 6 * size; ++y)
		// {
		// 	for (uint32_t x = 0; x < size; ++x)
		// 	{
		// 		uint32_t square_size = 64; // Size of each square
		// 		bool is_light_gray = ((x / square_size) % 2 == (y / square_size) % 2);

		// 		// Gray and light gray values
		// 		uint8_t gray = is_light_gray ? 192 : 128; // Light gray (192) and regular gray (128)
		// 		uint8_t a = 0xff;						  // Fully opaque alpha channel

		// 		// Set r, g, b to the same value for grayscale
		// 		data.emplace_back(uint32_t(gray) | (uint32_t(gray) << 8) | (uint32_t(gray) << 16) | (uint32_t(a) << 24));
		// 	}
		// }
		for (uint32_t i = 0; i < size * 6; i++)
		{
			uint8_t r = 0xff;
			uint8_t g = 0xff;
			uint8_t b = 0xff;
			uint8_t e = 143;
			data.emplace_back((uint32_t(r) << 24) | (uint32_t(g) << 16) | (uint32_t(b) << 8) | uint32_t(e));
		}

		assert(data.size() == size * 6 * size);

		// make a place for the texture to live on the GPU:
		Scene_env = rtg.helpers.create_cubemap_image(
			VkExtent2D{.width = size, .height = size}, // size of image
			VK_FORMAT_R8G8B8A8_UNORM,				   // how to interpret image data (in this case, linearly-encoded 8-bit RGBA)
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // should be device-local
			Helpers::Unmapped);

		// transfer data:
		rtg.helpers.transfer_to_cubemap_image(data.data(), sizeof(data[0]) * data.size(), Scene_env);
	}
}

void Tutorial::make_default_normal()
{
	std::vector<uint8_t> data = {128, 128, 255, 0};
	flat_normal_map = rtg.helpers.create_image(
		VkExtent2D{.width = 1, .height = 1}, // size of image
		VK_FORMAT_R8G8B8A8_UNORM,			 // how to interpret image data (in this case, linearly-encoded 8-bit RGBA)
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // should be device-local
		Helpers::Unmapped);

	// transfer data:
	rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), flat_normal_map);
}

void Tutorial::make_default_disp()
{
	uint8_t data = 1;

	flat_disp_map = rtg.helpers.create_image(
		VkExtent2D{.width = 1, .height = 1}, // size of image
		VK_FORMAT_R8_UNORM,					 // how to interpret image data (in this case, linearly-encoded 8-bit RGBA)
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // should be device-local
		Helpers::Unmapped);

	rtg.helpers.transfer_to_image((void *)&data, sizeof(data), flat_disp_map);
}

void Tutorial::setup_env_views_sample()
{
	{ // make image views for the textures
		VkImageViewCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.flags = 0,
			.image = Scene_env.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
			.format = Scene_env.format,
			// .components sets swizzling and is fine when zero-initialized
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 6,
			},
		};

		VK(vkCreateImageView(rtg.device, &create_info, nullptr, &Scene_env_view));
	}

	{ // make a sampler for the cubemap
		VkSamplerCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.flags = 0,
			.magFilter = VK_FILTER_NEAREST,
			.minFilter = VK_FILTER_NEAREST,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.mipLodBias = 0.0f,
			.anisotropyEnable = VK_FALSE,
			.maxAnisotropy = 0.0f, // doesn't matter if anisotropy isn't enabled
			.compareEnable = VK_FALSE,
			.compareOp = VK_COMPARE_OP_ALWAYS, // doesn't matter if compare isn't enabled
			.minLod = 0.0f,
			.maxLod = 5.0f,
			.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			.unnormalizedCoordinates = VK_FALSE,
		};
		VK(vkCreateSampler(rtg.device, &create_info, nullptr, &Scene_env_sampler));
	}

	if (Lamber_env.handle != VK_NULL_HANDLE)
	{
		{ // make image views for the textures
			VkImageViewCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.flags = 0,
				.image = Lamber_env.handle,
				.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
				.format = Lamber_env.format,
				// .components sets swizzling and is fine when zero-initialized
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 6,
				},
			};

			VK(vkCreateImageView(rtg.device, &create_info, nullptr, &Lamber_env_view));
		}
	}
}

void Tutorial::load_scene_object_textures()
{
	textures.reserve(s72_scene.textures_src.size() + 1);

	for (auto &t_src : s72_scene.textures_src)
	{
		if (t_src != "" && s72_scene.textures_src_index_map.find(t_src) == s72_scene.textures_src_index_map.end())
		{
			int w, h, n, ok;
			std::string filename = "./resource/" + t_src;
			[[maybe_unused]] unsigned char *image_data = stbi_load(filename.c_str(), &w, &h, &n, 0);
			ok = stbi_info(filename.c_str(), &w, &h, &n);
			std::cout << "texture: " << filename.c_str() << " ok? " << ok << ": " << w << ", " << h << ", " << n << "\n";

			std::vector<uint8_t> rgba_data;

			if (n == 1)
			{
				textures.emplace_back(rtg.helpers.create_image(
					VkExtent2D{.width = (uint32_t)w, .height = (uint32_t)h}, // size of image
					VK_FORMAT_R8_UNORM,										 // how to interpret image data (in this case, SRGB-encoded 8-bit RGBA)
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // should be device-local
					Helpers::Unmapped));

				rtg.helpers.transfer_to_image(image_data, w * h * n, textures.back());

				stbi_image_free(image_data);

				// bind textures index
				s72_scene.textures_src_index_map[t_src] = (uint32_t)textures.size() - 1;
				continue;
			}

			if (n == 3)
			{
				size_t size = w * h * n;
				rgba_data.reserve(w * h * 4);

				for (size_t i = 0, j = 0; i < size; i += 3, j += 4)
				{
					rgba_data[j] = ((uint8_t *)image_data)[i];		   // R
					rgba_data[j + 1] = ((uint8_t *)image_data)[i + 1]; // G
					rgba_data[j + 2] = ((uint8_t *)image_data)[i + 2]; // B
					rgba_data[j + 3] = 255;							   // A
				}
				// make a place for the texture to live on the GPU:
				textures.emplace_back(rtg.helpers.create_image(
					VkExtent2D{.width = (uint32_t)w, .height = (uint32_t)h},			  // size of image
					n == 3 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, // how to interpret image data (in this case, SRGB-encoded 8-bit RGBA)
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // should be device-local
					Helpers::Unmapped));

				rtg.helpers.transfer_to_image(rgba_data.data(), w * h * 4, textures.back());
			}

			else if (n == 4)
			{
				// convert to E5B9G9R9
				std::vector<uint32_t> e5b9g9r9_data = convertImageToE5B9G9R9(image_data, w, h);

				// make a place for the texture to live on the GPU:
				// TODO: convert to E5B9G9R9 format
				textures.emplace_back(rtg.helpers.create_image(
					VkExtent2D{.width = (uint32_t)w, .height = (uint32_t)h},	 // size of image
					n == 3 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB, // how to interpret image data (in this case, SRGB-encoded 8-bit RGBA)
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // should be device-local
					Helpers::Unmapped));

				rtg.helpers.transfer_to_image(image_data, w * h * n, textures.back());
			}

			stbi_image_free(image_data);

			// bind textures index
			s72_scene.textures_src_index_map[t_src] = (uint32_t)textures.size() - 1;
		}
	}
}

void Tutorial::setup_views_sample()
{
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
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.mipLodBias = 0.0f,
			.anisotropyEnable = VK_FALSE,
			.maxAnisotropy = 0.0f, // doesn't matter if anisotropy isn't enabled
			.compareEnable = VK_FALSE,
			.compareOp = VK_COMPARE_OP_ALWAYS, // doesn't matter if compare isn't enabled
			.minLod = 0.0f,
			.maxLod = 5.0f,
			.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			.unnormalizedCoordinates = VK_FALSE,
		};
		VK(vkCreateSampler(rtg.device, &create_info, nullptr, &texture_sampler));
	}
}

void Tutorial::setup_normal_views_sample()
{
	{ // make image views for the normal map
		VkImageViewCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.flags = 0,
			.image = flat_normal_map.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = flat_normal_map.format,
			// .components sets swizzling and is fine when zero-initialized
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};

		VK(vkCreateImageView(rtg.device, &create_info, nullptr, &flat_normal_view));
	}

	{ // make a sampler for the normal map
		VkSamplerCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.flags = 0,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.mipLodBias = 0.0f,
			.anisotropyEnable = VK_FALSE,
			.maxAnisotropy = 0.0f, // doesn't matter if anisotropy isn't enabled
			.compareEnable = VK_FALSE,
			.compareOp = VK_COMPARE_OP_ALWAYS, // doesn't matter if compare isn't enabled
			.minLod = 0.0f,
			.maxLod = 5.0f,
			.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			.unnormalizedCoordinates = VK_FALSE,
		};
		VK(vkCreateSampler(rtg.device, &create_info, nullptr, &normal_sampler));
	}
}

void Tutorial::setup_disp_views_sample()
{
	{ // make image views for the normal map
		VkImageViewCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.flags = 0,
			.image = flat_disp_map.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = flat_disp_map.format,
			// .components sets swizzling and is fine when zero-initialized
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};

		VK(vkCreateImageView(rtg.device, &create_info, nullptr, &flat_disp_view));
	}

	{ // make a sampler for the normal map
		VkSamplerCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.flags = 0,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.mipLodBias = 0.0f,
			.anisotropyEnable = VK_FALSE,
			.maxAnisotropy = 0.0f, // doesn't matter if anisotropy isn't enabled
			.compareEnable = VK_FALSE,
			.compareOp = VK_COMPARE_OP_ALWAYS, // doesn't matter if compare isn't enabled
			.minLod = 0.0f,
			.maxLod = 5.0f,
			.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			.unnormalizedCoordinates = VK_FALSE,
		};
		VK(vkCreateSampler(rtg.device, &create_info, nullptr, &disp_sampler));
	}
}

void Tutorial::setup_shadow_image()
{
	total_light_obj_cnt = 0;
	for (const auto &pair : s72_scene.light_node_map)
	{
		total_light_obj_cnt += pair.second.size(); // pair.second is the vector of Node*
	}

	shadow_map_grid_size = (uint32_t)(sqrt(total_light_obj_cnt) + 4) / 4 * 4;

	printf("shadow_map_grid_size: %d\n", shadow_map_grid_size);

	shadow_map = rtg.helpers.create_image(
		VkExtent2D{.width = SHADOW_MAP_WIDTH * shadow_map_grid_size, .height = SHADOW_MAP_HEIGHT * shadow_map_grid_size}, // size of image
		VK_FORMAT_D32_SFLOAT,																							  // how to interpret image data
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_SAMPLED_BIT,		 // will sample and DEPTH_STENCIL
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, // should be device-local
		Helpers::Unmapped);
}

void Tutorial::setup_shadow_views_sample()
{
	VkImageViewCreateInfo view_info = {};
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.pNext = NULL;
	view_info.image = shadow_map.handle;
	view_info.format = VK_FORMAT_D32_SFLOAT;
	view_info.components.r = VK_COMPONENT_SWIZZLE_R;
	view_info.components.g = VK_COMPONENT_SWIZZLE_G;
	view_info.components.b = VK_COMPONENT_SWIZZLE_B;
	view_info.components.a = VK_COMPONENT_SWIZZLE_A;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	view_info.subresourceRange.baseMipLevel = 0;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = 1;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.flags = 0;

	vkCreateImageView(rtg.device, &view_info, NULL, &shadow_view);

	VkSamplerCreateInfo sampler = {};
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.mipLodBias = 0.0f;
	sampler.maxAnisotropy = 1.0f;
	sampler.minLod = 0.0f;
	sampler.maxLod = 1.0f;
	sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	vkCreateSampler(rtg.device, &sampler, nullptr, &shadow_sampler);
}

void Tutorial::create_shadow_renderpass()
{
	VkAttachmentDescription attachmentDescription{};
	attachmentDescription.format = VK_FORMAT_D32_SFLOAT;
	attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
	attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;	  // Clear depth at beginning of the render pass
	attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // We will read from depth, so it's important to store the depth attachment results
	attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;					 // We don't care about initial layout of the attachment
	attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL; // Attachment will be transitioned to shader read at render pass end

	VkAttachmentReference depthReference = {};
	depthReference.attachment = 0;
	depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // Attachment will be used as depth/stencil during render pass

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 0;				   // No color attachments
	subpass.pDepthStencilAttachment = &depthReference; // Reference to our depth attachment

	VkRenderPassCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &attachmentDescription,
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 0,
		.pDependencies = nullptr,
	};

	VK(vkCreateRenderPass(rtg.device, &create_info, nullptr, &shadow_map_render_pass));
}

void Tutorial::create_shadow_framebuffer()
{
	VkFramebufferCreateInfo fb_info;
	fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fb_info.pNext = NULL;
	fb_info.renderPass = shadow_map_render_pass;
	fb_info.attachmentCount = 1;
	fb_info.pAttachments = &shadow_view;
	fb_info.width = SHADOW_MAP_WIDTH * shadow_map_grid_size;
	fb_info.height = SHADOW_MAP_HEIGHT * shadow_map_grid_size;
	fb_info.layers = 1;
	fb_info.flags = 0;

	vkCreateFramebuffer(rtg.device, &fb_info, NULL, &shadow_map_framebuffer);
}

void Tutorial::prepare_terrain()
{
	uint32_t length = (uint32_t)s72_scene.terrain.length;

	terrain_image = rtg.helpers.create_image(
		VkExtent3D{.width = length, .height = length, .depth = length}, // size of image
		VK_FORMAT_R8_UNORM,												// how to interpret image data (in this case, linearly-encoded 8-bit RGBA)
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // should be device-local
		Helpers::Unmapped);

	VkSamplerCreateInfo sampler = {};
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.mipLodBias = 0.0f;
	sampler.compareOp = VK_COMPARE_OP_NEVER;
	sampler.minLod = 0.0f;
	sampler.maxLod = 0.0f;
	sampler.maxAnisotropy = 1.0;
	sampler.anisotropyEnable = VK_FALSE;
	sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

	VK(vkCreateSampler(rtg.device, &sampler, nullptr, &terrain_sampler));

	// Create image view
	VkImageViewCreateInfo view = {};
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = terrain_image.handle;
	view.viewType = VK_IMAGE_VIEW_TYPE_3D;
	view.format = terrain_image.format;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.baseMipLevel = 0;
	view.subresourceRange.baseArrayLayer = 0;
	view.subresourceRange.layerCount = 1;
	view.subresourceRange.levelCount = 1;

	VK(vkCreateImageView(rtg.device, &view, nullptr, &terrain_view));
}

void Tutorial::setup_texture_descriptor_pool()
{
	// create the texture descriptor pool
	uint32_t per_texture = uint32_t(textures.size()); // for easier-to-read counting

	std::array<VkDescriptorPoolSize, 1> pool_sizes{
		VkDescriptorPoolSize{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 4 * 1 * per_texture, // 4 descriptors per set, one set per texture; binding 0 texture, binding 1 env cubemap
		},
	};

	VkDescriptorPoolCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = 0,					// because CREATE_FREE_DESCRIPTOR_SET_BIT isn't included, *can't* free individual descriptors allocated from this pool
		.maxSets = 2 * per_texture, // one set per texture **why i have adjust to 2 instead of 1**
		.poolSizeCount = uint32_t(pool_sizes.size()),
		.pPoolSizes = pool_sizes.data(),
	};

	VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &texture_descriptor_pool));
	// printf("setup_texture_descriptor_pool...done\n");
}

void Tutorial::make_texture_descriptor_sets()
{
	// allocate and write the texture descriptor sets
	// allocate the descriptors (using the same alloc_info):
	VkDescriptorSetAllocateInfo alloc_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = texture_descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &scenes_pipeline.set2_TEXTURE,
	};

	size_t texture_descriptor_size = s72_scene.material_textureindex_map.size();
	// printf("  %zd, ", texture_descriptor_size);
	//  auto size = textures.size(); // previous version
	texture_descriptors.assign(texture_descriptor_size + 1, VK_NULL_HANDLE);

	for (VkDescriptorSet &descriptor_set : texture_descriptors)
	{
		VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &descriptor_set));
	}

	// write descriptors for textures:
	// the last one is designed for default env and mirror
	std::vector<VkDescriptorImageInfo> texture_infos(texture_descriptor_size + 1);
	std::vector<VkDescriptorImageInfo> cubemap_infos(texture_descriptor_size + 1); // For cubemap textures
	std::vector<VkDescriptorImageInfo> normalmap_infos(texture_descriptor_size + 1);
	std::vector<VkDescriptorImageInfo> dispmap_infos(texture_descriptor_size + 1);
	std::vector<VkWriteDescriptorSet> writes((texture_descriptor_size + 1) * 4); // 4 writes per texture

	// printf("loop through  material_textureindex_map:\n");
	size_t i = 0;
	for (auto it = s72_scene.material_textureindex_map.begin(); it != s72_scene.material_textureindex_map.end(); it++)
	// for (Helpers::AllocatedImage const &image : textures)
	{
		// size_t
		// i = &image - &textures[0];
		MaterialType type = it->first->type;

		std::vector<int> texture_indexes = it->second;
		assert(texture_indexes.size() == 3);

		// bind to material_descriptor_index_map
		s72_scene.material_descriptor_index_map[it->first] = (int)i;

		// Regular texture
		texture_infos[i] = VkDescriptorImageInfo{
			.sampler = texture_sampler,
			.imageView = texture_views[texture_indexes[0]],
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		// Cubemap texture (assuming you have cubemap views and sampler set up)
		cubemap_infos[i] = VkDescriptorImageInfo{
			.sampler = Scene_env_sampler, // Cubemap-specific sampler
			.imageView = Scene_env_view,  // Image view for the cubemap
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
		if (type == LAMBERTIAN && Lamber_env_view)
		{
			printf("index %zd: lamber, %d, %d\n", i, texture_indexes[0], texture_indexes[1]);
			cubemap_infos[i].imageView = Lamber_env_view;
		}

		// normalmap texture
		if (texture_indexes[1] >= 0)
		{
			assert(texture_views[texture_indexes[1]] != VK_NULL_HANDLE);
			normalmap_infos[i] = VkDescriptorImageInfo{
				.sampler = normal_sampler,
				.imageView = texture_views[texture_indexes[1]],
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
		}
		else
		{
			assert(flat_normal_view != VK_NULL_HANDLE);
			normalmap_infos[i] = VkDescriptorImageInfo{
				.sampler = normal_sampler,
				.imageView = flat_normal_view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
		}

		// displacement texture
		if (texture_indexes[2] >= 0)
		{
			assert(texture_views[texture_indexes[2]] != VK_NULL_HANDLE);
			dispmap_infos[i] = VkDescriptorImageInfo{
				.sampler = disp_sampler,
				.imageView = texture_views[texture_indexes[2]],
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
		}
		else
		{
			assert(flat_disp_view != VK_NULL_HANDLE);
			dispmap_infos[i] = VkDescriptorImageInfo{
				.sampler = disp_sampler,
				.imageView = flat_disp_view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
		}

		// Write for regular texture (binding = 0)
		writes[i * 4] = VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = texture_descriptors[i],
			.dstBinding = 0, // Binding 0 for regular texture
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &texture_infos[i],
		};

		// Write for cubemap texture (binding = 1)
		writes[i * 4 + 1] = VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = texture_descriptors[i],
			.dstBinding = 1, // Binding 1 for cubemap texture
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &cubemap_infos[i],
		};

		// Write for normal (binding = 2)
		writes[i * 4 + 2] = VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = texture_descriptors[i],
			.dstBinding = 2, // Binding 2 for normal
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &normalmap_infos[i],
		};

		// Write for disp (binding = 3)
		writes[i * 4 + 3] = VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = texture_descriptors[i],
			.dstBinding = 3, // Binding 3 for disp
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &dispmap_infos[i],
		};

		i++;
	}

	{ // the last one, for env and mirror type
		// Regular texture
		texture_infos[texture_descriptor_size] = VkDescriptorImageInfo{
			.sampler = texture_sampler,
			.imageView = texture_views[0], // default texture
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		// Cubemap texture (assuming you have cubemap views and sampler set up)
		cubemap_infos[texture_descriptor_size] = VkDescriptorImageInfo{
			.sampler = Scene_env_sampler, // Cubemap-specific sampler
			.imageView = Scene_env_view,  // Image view for the cubemap
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		// normalmap texture
		assert(flat_normal_view != VK_NULL_HANDLE);
		normalmap_infos[texture_descriptor_size] = VkDescriptorImageInfo{
			.sampler = normal_sampler,
			.imageView = flat_normal_view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		// disp texture
		assert(flat_disp_view != VK_NULL_HANDLE);
		dispmap_infos[texture_descriptor_size] = VkDescriptorImageInfo{
			.sampler = disp_sampler,
			.imageView = flat_disp_view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		// Write for regular texture (binding = 0)
		writes[texture_descriptor_size * 4] = VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = texture_descriptors[texture_descriptor_size],
			.dstBinding = 0, // Binding 0 for regular texture
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &texture_infos[texture_descriptor_size],
		};

		// Write for cubemap texture (binding = 1)
		writes[texture_descriptor_size * 4 + 1] = VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = texture_descriptors[texture_descriptor_size],
			.dstBinding = 1, // Binding 1 for cubemap texture
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &cubemap_infos[texture_descriptor_size],
		};

		// Write for normal (binding = 2)
		writes[texture_descriptor_size * 4 + 2] = VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = texture_descriptors[texture_descriptor_size],
			.dstBinding = 2, // Binding 2 for normal
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &normalmap_infos[texture_descriptor_size],
		};

		// Write for disp (binding = 3)
		writes[texture_descriptor_size * 4 + 3] = VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = texture_descriptors[texture_descriptor_size],
			.dstBinding = 3, // Binding 3 for disp
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &dispmap_infos[texture_descriptor_size],
		};
	}

	vkUpdateDescriptorSets(rtg.device, uint32_t(writes.size()), writes.data(), 0, nullptr);
}

void Tutorial::make_default_texture()
{
	{ // texture 0 will be a dark grey / light grey checkerboard with a red square at the origin.
		// actually make the texture:
		uint32_t size = 128;

		std::vector<uint32_t> data;
		data.reserve(size * size);
		for (uint32_t y = 0; y < size; ++y)
		{
			for (uint32_t x = 0; x < size; ++x)
			{
				uint32_t square_size = 64; // Size of each square
				bool is_light_gray = ((x / square_size) % 2 == (y / square_size) % 2);

				// Gray and light gray values
				uint8_t gray = is_light_gray ? 255 : 168; // Light gray (192) and regular gray (128)
				uint8_t a = 0xff;						  // Fully opaque alpha channel

				// Set r, g, b to the same value for grayscale
				data.emplace_back(uint32_t(gray) | (uint32_t(gray) << 8) | (uint32_t(gray) << 16) | (uint32_t(a) << 24));
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
			float edge_rotation_speed = 0.05f; // Adjust this value to control how fast the camera rotates at the edge

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
		constexpr float PlayerSpeed = 6.f;
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

		s72_scene.transforms[node_] = node_->make_local_to_world();

		node_->child_forward_kinematics_transforms(node_);
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
	std::cout << s72_file << std::endl;
	sejp::value val = sejp::load(s72_file);
	scene_workflow(val);
	// std::map<std::string, sejp::value> const &object = val.as_object().value();
}

// Mesh* ~ BBox (1: 1), Mesh* ~ MeshVertices (1: 1)
void Tutorial::set_mesh_vertices_map(std::vector<SceneVertex> &vertices)
{
	s72_scene.mesh_vertices_map.clear();

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
		MeshVertices mesh_vertices;
		BBox bbox;

		vertices.reserve(vertices.size() + mesh.count);
		mesh_vertices.first = static_cast<uint32_t>(vertices.size());

		size_t total_size = mesh.count * stride;
		std::vector<char> total_buffer(total_size);
		file.read(total_buffer.data(), total_size);

		// Read each vertex by its stride
		for (uint32_t i = 0; i < mesh.count; ++i)
		{
			SceneVertex vertex;

			// Read a stride worth of data into a buffer
			const char *buffer = total_buffer.data() + i * stride;

			// Extract Position
			const auto &pos_attr = mesh.attributes.at("POSITION");
			std::memcpy(&vertex.Position, buffer + pos_attr.offset, sizeof(vertex.Position));

			// include vertex in bbox
			bbox.enclose(glm::vec3(vertex.Position.x, vertex.Position.y, vertex.Position.z));

			// Extract Normal
			const auto &normal_attr = mesh.attributes.at("NORMAL");
			std::memcpy(&vertex.Normal, buffer + normal_attr.offset, sizeof(vertex.Normal));

			// Extract Tangent
			const auto &tangent_attr = mesh.attributes.at("TANGENT");
			std::memcpy(&vertex.Tangent, buffer + tangent_attr.offset, sizeof(vertex.Tangent));

			// Extract TexCoord
			const auto &texcoord_attr = mesh.attributes.at("TEXCOORD");
			std::memcpy(&vertex.TexCoord, buffer + texcoord_attr.offset, sizeof(vertex.TexCoord));

			// Extract Color if present
			if (has_color)
			{
				glm::u8vec4 color;
				const auto &color_attr = mesh.attributes.at("COLOR");
				std::memcpy(&color, buffer + color_attr.offset, sizeof(color));
				vertex.color = std::optional<Color>{{color.r, color.g, color.b, color.a}};
			}

			// Push the vertex into the vertices vector
			vertices.push_back(vertex);
		}
		assert(vertices.size() - mesh_vertices.first == mesh.count);

		// save in global
		s72_scene.mesh_bbox_map[&mesh] = bbox;
		printf("bbox %s: min: %f, %f, %f; max: %f, %f, %f\n", mesh.name.c_str(), bbox.min.x, bbox.min.y, bbox.min.z,
			   bbox.max.x, bbox.max.y, bbox.max.z);

		// Set vertex count in object_vertices
		mesh_vertices.count = mesh.count;

		s72_scene.mesh_vertices_map[&mesh] = mesh_vertices;

		// std::cout << mesh.name << " --read file-- " << b72_file_path << "vertices size: "
		//		  << mesh_vertices.count << "\n";

		file.close();
	}

	for (auto &i : s72_scene.mesh_bbox_map)
	{
		printf("bbox %s: mid: %f, %f, %f; r: %f\n", i.first->name.c_str(),
			   i.second.center().x, i.second.center().y, i.second.center().z,
			   glm::distance(i.second.max, i.second.min) * 0.5f);
	}
}

// build up vector<SceneObject> scene_objects, setup s72_scene.transforms Node* ~ mat4
void Tutorial::set_scene_objects(std::vector<SceneVertex> &vertices)
{
	s72_scene.transforms.clear();
	scene_objects.clear();

	for (auto &node : s72_scene.nodes)
	{
		auto local_to_world = node.make_local_to_world();

		glm::vec4 extra_column = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		glm::mat4 combined_matrix = glm::mat4(
			glm::vec4(local_to_world[0], extra_column[0]),
			glm::vec4(local_to_world[1], extra_column[1]),
			glm::vec4(local_to_world[2], extra_column[2]),
			glm::vec4(local_to_world[3], extra_column[3]));

		// save in global
		s72_scene.transforms[&node] = combined_matrix;

		if (node.mesh_ == nullptr)
			continue;
		MeshVertices mesh_vertices = s72_scene.mesh_vertices_map[node.mesh_];

		SceneObject scene_object;

		scene_object.scene_object_vertices.first = mesh_vertices.first;
		scene_object.scene_object_vertices.count = mesh_vertices.count;

		scene_object.transform = combined_matrix;
		scene_object.object_node_ = &node;
		scene_object.object_mesh_ = node.mesh_;

		uint32_t index = uint32_t(&node - &s72_scene.nodes[0]);

		printf("index%d %s: %s \n", index, node.name.c_str(), node.mesh_->name.c_str());

		scene_objects.push_back(scene_object);
	}
}

// Mesh* ~ MaterialObject* (1: 1)
void Tutorial::set_mesh_material_map()
{
	assert(s72_scene.materials.size() > 0);

	for (auto &mesh : s72_scene.meshes)
	{
		if (mesh.material != "")
		{
			for (auto &meterial_obj : s72_scene.materials)
			{
				if (meterial_obj.name == mesh.material)
				{
					s72_scene.mesh_material_map[&mesh] = &meterial_obj;
				}
			}
		}
		else
		{
			// bind to the defualt material, i.e. the last material
			s72_scene.mesh_material_map[&mesh] = &(s72_scene.materials.back());
		}
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

	scene_object.transform = combined_matrix;
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

std::vector<uint32_t> Tutorial::convertImageToE5B9G9R9(const unsigned char *image_data, int width, int height)
{
	std::vector<uint32_t> e5b9g9r9_data(width * height);

	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			int idx = (y * width + x) * 4; // 4 channels for r8g8b8e8

			// Extract R, G, B, and E channels from image_data
			float r = image_data[idx] / 255.0f;
			float g = image_data[idx + 1] / 255.0f;
			float b = image_data[idx + 2] / 255.0f;
			int e8 = image_data[idx + 3];

			// Convert e8 to a scale factor for RGB
			float scale = std::pow(2.0f, e8 - 128.f); // 128 bias for 8-bit exponent

			// Scale RGB by the exponent
			r *= scale;
			g *= scale;
			b *= scale;

			// Convert to e5b9g9r9 and store in output
			e5b9g9r9_data[y * width + x] = convertToE5B9G9R9(r, g, b);
		}
	}

	return e5b9g9r9_data;
}
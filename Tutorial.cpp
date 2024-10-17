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

		setup_views_sample();
	}

	setup_texture_descriptor_pool();

	make_texture_descriptor_sets();

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

				{ // push materialType:
					// std::cout << "PUSH: " << inst.material_->type;

					ScenesPipeline::Push push{
						.materialType = inst.material_->type,
					};
					vkCmdPushConstants(workspace.command_buffer, scenes_pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);

					// std::cout << " done!" << std::endl;
				}

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
				}
			}

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
				if (playmode.camera_mode == DEBUG || playmode.cull_mode == FRUSTUM) // (playmode.cull_mode == FRUSTUM)
				{
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
					.mesh_ = mesh_,
					.material_ = material_obj_,
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
}

void Tutorial::create_description_pool()
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
	rtg.helpers.transfer_to_cubemap_image(image_data, w * h * n, Scene_env); /// todo modify it

	stbi_image_free(image_data);
}

void Tutorial::make_default_environ()
{
	{ // texture 0 will be a dark grey / light grey checkerboard with a red square at the origin.
		// actually make the texture:
		uint32_t size = 128;

		std::vector<uint32_t> data;
		data.reserve(size * 6 * size);
		for (uint32_t y = 0; y < 6 * size; ++y)
		{
			for (uint32_t x = 0; x < size; ++x)
			{
				uint32_t square_size = 64; // Size of each square
				bool is_light_gray = ((x / square_size) % 2 == (y / square_size) % 2);

				// Gray and light gray values
				uint8_t gray = is_light_gray ? 255 : 128; // Light gray (192) and regular gray (128)
				uint8_t a = 0xff;						  // Fully opaque alpha channel

				// Set r, g, b to the same value for grayscale
				data.emplace_back(uint32_t(gray) | (uint32_t(gray) << 8) | (uint32_t(gray) << 16) | (uint32_t(a) << 24));
			}
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
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
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
			std::cout << "texture: " << filename.c_str() << " ok? " << ok << ": " << w << ", " << h << ", " << n;

			std::vector<uint8_t> rgba_data;
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
			}

			// make a place for the texture to live on the GPU:
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{.width = (uint32_t)w, .height = (uint32_t)h},	 // size of image
				n == 3 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB, // how to interpret image data (in this case, SRGB-encoded 8-bit RGBA)
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // will sample and upload
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,						  // should be device-local
				Helpers::Unmapped));

			// transfer data:
			if (n == 3)
			{
				rtg.helpers.transfer_to_image(rgba_data.data(), w * h * 4, textures.back());
			}
			else
			{
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
}

void Tutorial::setup_texture_descriptor_pool()
{
	// create the texture descriptor pool
	uint32_t per_texture = uint32_t(textures.size()); // for easier-to-read counting

	std::array<VkDescriptorPoolSize, 1> pool_sizes{
		VkDescriptorPoolSize{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 2 * 1 * per_texture, // two descriptors per set, one set per texture; binding 0 texture, binding 1 env cubemap
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
	texture_descriptors.assign(textures.size(), VK_NULL_HANDLE);
	for (VkDescriptorSet &descriptor_set : texture_descriptors)
	{
		VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &descriptor_set));
	}

	// write descriptors for textures:
	std::vector<VkDescriptorImageInfo> texture_infos(textures.size());
	std::vector<VkDescriptorImageInfo> cubemap_infos(textures.size()); // For cubemap textures
	std::vector<VkWriteDescriptorSet> writes(textures.size() * 2);	   // 2 writes per texture

	for (Helpers::AllocatedImage const &image : textures)
	{
		size_t i = &image - &textures[0];

		// Regular texture
		texture_infos[i] = VkDescriptorImageInfo{
			.sampler = texture_sampler,
			.imageView = texture_views[i],
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		// Cubemap texture (assuming you have cubemap views and sampler set up)
		cubemap_infos[i] = VkDescriptorImageInfo{
			.sampler = Scene_env_sampler, // Cubemap-specific sampler
			.imageView = Scene_env_view,  // Image view for the cubemap
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		// Write for regular texture (binding = 0)
		writes[i * 2] = VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = texture_descriptors[i],
			.dstBinding = 0, // Binding 0 for regular texture
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &texture_infos[i],
		};

		// Write for cubemap texture (binding = 1)
		writes[i * 2 + 1] = VkWriteDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = texture_descriptors[i],
			.dstBinding = 1, // Binding 1 for cubemap texture
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &cubemap_infos[i],
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
		constexpr float PlayerSpeed = 10.f;
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

		// Set vertex count in object_vertices
		mesh_vertices.count = mesh.count;

		s72_scene.mesh_vertices_map[&mesh] = mesh_vertices;

		// std::cout << mesh.name << " --read file-- " << b72_file_path << "vertices size: "
		//		  << mesh_vertices.count << "\n";

		file.close();
	}
}

// build up vector<SceneObject> scene_objects, setup s72_scene.transforms Node* ~ mat4
void Tutorial::set_scene_objects(std::vector<SceneVertex> &vertices)
{
	s72_scene.transforms.clear();
	scene_objects.clear();

	for (auto &node : s72_scene.nodes)
	{
		if (node.mesh_ == nullptr)
			continue;

		MeshVertices mesh_vertices = s72_scene.mesh_vertices_map[node.mesh_];

		SceneObject scene_object;

		scene_object.scene_object_vertices.first = mesh_vertices.first;
		scene_object.scene_object_vertices.count = mesh_vertices.count;

		auto local_to_world = node.make_local_to_world();

		glm::vec4 extra_column = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		glm::mat4 combined_matrix = glm::mat4(
			glm::vec4(local_to_world[0], extra_column[0]),
			glm::vec4(local_to_world[1], extra_column[1]),
			glm::vec4(local_to_world[2], extra_column[2]),
			glm::vec4(local_to_world[3], extra_column[3]));

		scene_object.transform = combined_matrix;
		scene_object.object_node_ = &node;
		scene_object.object_mesh_ = node.mesh_;

		scene_objects.push_back(scene_object);

		// save in global
		s72_scene.transforms[&node] = combined_matrix;
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
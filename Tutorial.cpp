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

	{															  // create descriptor pool:
		uint32_t per_workspace = uint32_t(rtg.workspaces.size()); // for easier-to-read counting

		std::array<VkDescriptorPoolSize, 1> pool_sizes{
			// we only need uniform buffer descriptors for the moment:
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1 * per_workspace, // one descriptor per set, one set per workspace
			},
		};

		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0,					  // because CREATE_FREE_DESCRIPTOR_SET_BIT isn't included, *can't* free individual descriptors allocated from this pool
			.maxSets = 1 * per_workspace, // one set per workspace
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

		{ // point descriptor to Camera buffer:
			VkDescriptorBufferInfo Camera_info{
				.buffer = workspace.Camera.handle,
				.offset = 0,
				.range = workspace.Camera.size,
			};

			std::array<VkWriteDescriptorSet, 1> writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Camera_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &Camera_info,
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
}

Tutorial::~Tutorial()
{
	// just in case rendering is still in flight, don't destroy resources:
	//(not using VK macro to avoid throw-ing in destructor)
	if (VkResult result = vkDeviceWaitIdle(rtg.device); result != VK_SUCCESS)
	{
		std::cerr << "Failed to vkDeviceWaitIdle in Tutorial::~Tutorial [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
	}

	if (swapchain_depth_image.handle != VK_NULL_HANDLE)
	{
		destroy_framebuffers();
	}

	background_pipeline.destroy(rtg);
	lines_pipeline.destroy(rtg);

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
			vkCmdDraw(workspace.command_buffer, uint32_t(lines_vertices.size()), 1, 0, 0);
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
		float ang = float(M_PI) * 2.0f * 6.0f * (time / 60.0f);
		CLIP_FROM_WORLD = perspective(
							  60.0f / float(M_PI) * 180.0f,									   // vfov
							  rtg.swapchain_extent.width / float(rtg.swapchain_extent.height), // aspect
							  0.1f,															   // near
							  1000.0f														   // far
							  ) *
						  look_at(
							  3.0f * sin(time / 5.f) * std::cos(ang), 3.0f * std::sin(ang), sin(time / 5.f) - 2.f, // eye
							  0.0f, 0.0f, 0.5f,																	   // target
							  0.0f, 0.0f, 1.0f																	   // up
						  );
	}

	// { // make some crossing lines at different depths:
	// 	lines_vertices.clear();
	// 	constexpr size_t count = 2 * 30 + 2 * 30;
	// 	lines_vertices.reserve(count);
	// 	// horizontal lines at z = 0.5f:
	// 	for (uint32_t i = 0; i < 30; ++i)
	// 	{
	// 		float y_base = (i + 0.5f) / 30.0f * 2.0f - 1.0f;
	// 		float z = (i + 0.5f) / 30.0f;
	// 		float y_offset = std::sin(time + z * 3.14159f * 2.0f) * 0.2f; // Adjust the amplitude and frequency as needed

	// 		lines_vertices.emplace_back(PosColVertex{
	// 			.Position{.x = -1.0f, .y = y_base + y_offset, .z = 0.5f},
	// 			.Color{.r = 0x1f, .g = 0xff, .b = 0x00, .a = 0xff},
	// 		});
	// 		lines_vertices.emplace_back(PosColVertex{
	// 			.Position{.x = 1.0f, .y = y_base - y_offset, .z = 0.5f},
	// 			.Color{.r = 0x1f, .g = 0x0f, .b = 0xf0, .a = 0xff},
	// 		});
	// 	}
	// 	// vertical lines at z = 0.0f (near) through 1.0f (far):
	// 	for (uint32_t i = 0; i < 30; ++i)
	// 	{
	// 		float x_base = (i + 0.5f) / 30.0f * 2.0f - 1.0f;
	// 		float z = (i + 0.5f) / 30.0f;
	// 		float x_offset = std::sin(time + z * 3.14159f * 2.0f) * 0.2f; // Adjust the amplitude and frequency as needed

	// 		lines_vertices.emplace_back(PosColVertex{
	// 			.Position{.x = x_base + x_offset, .y = -1.0f, .z = z},
	// 			.Color{.r = 0x04, .g = 0x00, .b = 0x0f, .a = 0xff},
	// 		});
	// 		lines_vertices.emplace_back(PosColVertex{
	// 			.Position{.x = x_base - x_offset, .y = 1.0f, .z = z},
	// 			.Color{.r = 0x04, .g = 0x00, .b = 0xff, .a = 0xff},
	// 		});
	// 	}
	// 	assert(lines_vertices.size() == count);
	// }
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
}

void Tutorial::on_input(InputEvent const &)
{
}

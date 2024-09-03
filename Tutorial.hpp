#pragma once
#include "PosColVertex.hpp"
#include "mat4.hpp"
#include "RTG.hpp"

struct Tutorial : RTG::Application
{

	Tutorial(RTG &);
	Tutorial(Tutorial const &) = delete; // you shouldn't be copying this object
	~Tutorial();

	// kept for use in destructor:
	RTG &rtg;

	//--------------------------------------------------------------------
	// Resources that last the lifetime of the application:

	// chosen format for depth buffer:
	VkFormat depth_format{};
	// Render passes describe how pipelines write to images:
	VkRenderPass render_pass = VK_NULL_HANDLE;

	// Pipelines:

	// TODO
	struct BackgroundPipeline
	{
		// no descriptor set layouts

		struct Push
		{
			float time;
		};

		VkPipelineLayout layout = VK_NULL_HANDLE;

		// no vertex bindings

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} background_pipeline;

	struct LinesPipeline
	{
		// descriptor set layouts:
		VkDescriptorSetLayout set0_Camera = VK_NULL_HANDLE;

		// types for descriptors:
		struct Camera
		{
			mat4 CLIP_FROM_WORLD;
		};
		static_assert(sizeof(Camera) == 16 * 4, "camera buffer structure is packed");

		// no push constants

		VkPipelineLayout layout = VK_NULL_HANDLE;

		using Vertex = PosColVertex;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} lines_pipeline;

	// pools from which per-workspace things are allocated:
	VkCommandPool command_pool = VK_NULL_HANDLE;

	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

	// workspaces hold per-render resources:
	struct Workspace
	{
		VkCommandBuffer command_buffer = VK_NULL_HANDLE; // from the command pool above; reset at the start of every render.

		// location for lines data: (streamed to GPU per-frame)
		Helpers::AllocatedBuffer lines_vertices_src; // host coherent; mapped
		Helpers::AllocatedBuffer lines_vertices;	 // device-local

		// location for LinesPipeline::Camera data: (streamed to GPU per-frame)
		Helpers::AllocatedBuffer Camera_src; // host coherent; mapped
		Helpers::AllocatedBuffer Camera;	 // device-local
		VkDescriptorSet Camera_descriptors;	 // references Camera
	};
	std::vector<Workspace> workspaces;

	//-------------------------------------------------------------------
	// static scene resources:

	//--------------------------------------------------------------------
	// Resources that change when the swapchain is resized:

	virtual void on_swapchain(RTG &, RTG::SwapchainEvent const &) override;

	Helpers::AllocatedImage swapchain_depth_image;
	VkImageView swapchain_depth_image_view = VK_NULL_HANDLE;
	std::vector<VkFramebuffer> swapchain_framebuffers;
	// used from on_swapchain and the destructor: (framebuffers are created in on_swapchain)
	void destroy_framebuffers();

	//--------------------------------------------------------------------
	// Resources that change when time passes or the user interacts:

	virtual void update(float dt) override;
	virtual void on_input(InputEvent const &) override;

	float time = 0.0f;

	mat4 CLIP_FROM_WORLD;

	std::vector<LinesPipeline::Vertex> lines_vertices;

	//--------------------------------------------------------------------
	// Rendering function, uses all the resources above to queue work to draw a frame:

	virtual void render(RTG &, RTG::RenderParams const &) override;
};

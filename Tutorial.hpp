#pragma once
#include "PosColVertex.hpp"
#include "PosNorTexVertex.hpp"
#include "SceneVertex.hpp"
#include "mat4.hpp"
#include "RTG.hpp"

// Forward declarations of the structs
struct Node;
struct Mesh;
struct Camera;
struct S72_scene;

// Declare the global variable
extern S72_scene s72_scene;

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

	struct ObjectsPipeline
	{
		// descriptor set layouts:
		VkDescriptorSetLayout set0_World = VK_NULL_HANDLE;
		VkDescriptorSetLayout set1_Transforms = VK_NULL_HANDLE;
		VkDescriptorSetLayout set2_TEXTURE = VK_NULL_HANDLE;

		// types for descriptors:
		struct World
		{
			struct
			{
				float x, y, z, padding_;
			} SKY_DIRECTION;
			struct
			{
				float r, g, b, padding_;
			} SKY_ENERGY;
			struct
			{
				float x, y, z, padding_;
			} SUN_DIRECTION;
			struct
			{
				float r, g, b, padding_;
			} SUN_ENERGY;
		};
		static_assert(sizeof(World) == 4 * 4 + 4 * 4 + 4 * 4 + 4 * 4, "World is the expected size.");

		// types for descriptors:
		// using Camera = LinesPipeline::Camera;
		struct Transform
		{
			mat4 CLIP_FROM_LOCAL;
			mat4 WORLD_FROM_LOCAL;
			mat4 WORLD_FROM_LOCAL_NORMAL;
		};
		static_assert(sizeof(Transform) == 16 * 4 + 16 * 4 + 16 * 4, "Transform is the expected size.");

		// no push constants

		VkPipelineLayout layout = VK_NULL_HANDLE;

		using Vertex = PosNorTexVertex;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} objects_pipeline;

	struct ScenesPipeline
	{
		// descriptor set layouts:
		VkDescriptorSetLayout set0_World = VK_NULL_HANDLE;
		VkDescriptorSetLayout set1_Transforms = VK_NULL_HANDLE;
		VkDescriptorSetLayout set2_TEXTURE = VK_NULL_HANDLE;

		// types for descriptors:
		struct World
		{
			struct
			{
				float x, y, z, padding_;
			} SKY_DIRECTION;
			struct
			{
				float r, g, b, padding_;
			} SKY_ENERGY;
			struct
			{
				float x, y, z, padding_;
			} SUN_DIRECTION;
			struct
			{
				float r, g, b, padding_;
			} SUN_ENERGY;
		};

		// types for descriptors:
		// using Camera = LinesPipeline::Camera;
		struct Transform
		{
			mat4 CLIP_FROM_LOCAL;
			mat4 WORLD_FROM_LOCAL;
			mat4 WORLD_FROM_LOCAL_NORMAL;
			mat4 WORLD_FROM_LOCAL_TANGENT;
		};
		static_assert(sizeof(Transform) == 16 * 4 + 16 * 4 + 16 * 4 + 16 * 4, "Transform is the expected size.");

		// push constants
		struct Push
		{
			int use_out_color;
		};

		VkPipelineLayout layout = VK_NULL_HANDLE;

		using Vertex = SceneVertex;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} scenes_pipeline;

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

		// location for ObjectsPipeline::World data: (streamed to GPU per-frame)
		Helpers::AllocatedBuffer World_src; // host coherent; mapped
		Helpers::AllocatedBuffer World;		// device-local
		VkDescriptorSet World_descriptors;	// references World

		// location for ObjectsPipeline::Transforms data: (streamed to GPU per-frame)
		Helpers::AllocatedBuffer Transforms_src; // host coherent; mapped
		Helpers::AllocatedBuffer Transforms;	 // device-local
		VkDescriptorSet Transforms_descriptors;	 // references Transforms

		// // location for ScenesPipeline::Camera data: (streamed to GPU per-frame)
		// Helpers::AllocatedBuffer Scene_camera_src; // host coherent; mapped
		// Helpers::AllocatedBuffer Scene_camera;	   // device-local
		// VkDescriptorSet Scene_camera_descriptors;  // references Camera

		// location for ObjectsPipeline::World data: (streamed to GPU per-frame)
		Helpers::AllocatedBuffer Scene_world_src; // host coherent; mapped
		Helpers::AllocatedBuffer Scene_world;	  // device-local
		VkDescriptorSet Scene_world_descriptors;  // references World

		// location for ObjectsPipeline::Transforms data: (streamed to GPU per-frame)
		Helpers::AllocatedBuffer Scene_transforms_src; // host coherent; mapped
		Helpers::AllocatedBuffer Scene_transforms;	   // device-local
		VkDescriptorSet Scene_transforms_descriptors;  // references Transforms
	};
	std::vector<Workspace> workspaces;

	//-------------------------------------------------------------------
	// static scene resources:
	Helpers::AllocatedBuffer object_vertices;

	Helpers::AllocatedBuffer scene_vertices;

	struct ObjectVertices
	{
		uint32_t first = 0;
		uint32_t count = 0;
	};

	ObjectVertices plane_vertices;
	ObjectVertices torus_vertices;

	std::vector<ObjectVertices> scene_object_vertices;
	std::vector<mat4> scene_world_from_local; // one scene object to one mat4

	std::vector<Helpers::AllocatedImage> textures;
	std::vector<VkImageView> texture_views;
	VkSampler texture_sampler = VK_NULL_HANDLE;
	VkDescriptorPool texture_descriptor_pool = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> texture_descriptors; // allocated from texture_descriptor_pool

	void load_s72();
	void process_node(std::vector<SceneVertex> &vertices, Node *node);
	void load_vertex_from_b72(std::vector<SceneVertex> &vertices);
	//--------------------------------------------------------------------
	//  Resources that change when the swapchain is resized:

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

	ObjectsPipeline::World world;

	struct ObjectInstance
	{
		ObjectVertices vertices;
		ObjectsPipeline::Transform transform;
		uint32_t texture = 0;
	};
	std::vector<ObjectInstance> object_instances;

	struct ScenesObjectInstance
	{
		ObjectVertices vertices;
		ScenesPipeline::Transform transform;
		uint32_t texture = 0;
	};
	std::vector<ScenesObjectInstance> scene_instances;

	//--------------------------------------------------------------------
	// Rendering function, uses all the resources above to queue work to draw a frame:

	virtual void render(RTG &, RTG::RenderParams const &) override;
};

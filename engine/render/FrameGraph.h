#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::core { class Profiler; }

namespace engine::render
{
	/// Frame Graph resource ID (0 = invalid).
	using ResourceId = uint32_t;
	constexpr ResourceId kInvalidResourceId = 0;

	/// Number of bloom mip levels (full, 1/2, …, 1/32). Used by FG and Engine; defined here to avoid including BloomPass.h.
	constexpr uint32_t kBloomMipCount = 6;

	/// Usage state for image resources (maps to layout + stage/access for barriers).
	/// MVP: conservative barriers between passes; correct transitions for COLOR_ATTACHMENT, SAMPLED, TRANSFER.
	enum class ImageUsage
	{
		ColorWrite,
		DepthWrite,
		SampledRead,
		TransferSrc,
		TransferDst,
	};

	/// Last known usage state per resource (layout, stage, access) for barrier insertion.
	/// Uses sync2 types (VkPipelineStageFlags2 / VkAccessFlags2) for vkCmdPipelineBarrier2; fallback converts to legacy when needed.
	struct ResourceUsageState
	{
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkPipelineStageFlags2 stageMask2 = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
		VkAccessFlags2 accessMask2 = 0;
	};

	/// Descriptor for a logical image resource (transient by default, frame lifetime).
	struct ImageDesc
	{
		VkFormat format = VK_FORMAT_UNDEFINED;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t layers = 1;
		uint32_t mipLevels = 1;
		VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
		bool transient = true;
		/// When true, image is used as depth attachment (view aspect DEPTH_BIT, barriers use depth layout).
		bool isDepthAttachment = false;
		/// When > 0, effective extent is (extent.width >> extentScalePower, extent.height >> extentScalePower). Used e.g. for bloom mip pyramid (M08.1). Ignored if width/height are set.
		uint32_t extentScalePower = 0;
	};

	/// Descriptor for a logical buffer resource (transient by default, frame lifetime).
	struct BufferDesc
	{
		VkDeviceSize size = 0;
		VkBufferUsageFlags usage = 0;
		bool transient = true;
	};

	/// Resolves Frame Graph resource IDs to Vulkan handles (valid during the frame).
	/// Filled by FrameGraph::execute() for FG-created resources; external resources (e.g. swapchain)
	/// must be bound each frame via bindImage / bindBuffer.
	class Registry
	{
	public:
		Registry() = default;

		/// Binds an image (and view) for the given resource ID (e.g. swapchain image each frame).
		void bindImage(ResourceId id, VkImage image, VkImageView view);

		/// Binds a buffer for the given resource ID.
		void bindBuffer(ResourceId id, VkBuffer buffer);

		/// Returns the VkImage for the resource, or VK_NULL_HANDLE if not bound.
		VkImage getImage(ResourceId id) const;

		/// Returns the VkImageView for the resource, or VK_NULL_HANDLE if not bound.
		VkImageView getImageView(ResourceId id) const;

		/// Returns the VkBuffer for the resource, or VK_NULL_HANDLE if not bound.
		VkBuffer getBuffer(ResourceId id) const;

		/// Clears all bindings (call at start of frame before filling).
		void clear();

	private:
		struct ImageBinding
		{
			VkImage image = VK_NULL_HANDLE;
			VkImageView view = VK_NULL_HANDLE;
		};
		std::unordered_map<ResourceId, ImageBinding> m_images;
		std::unordered_map<ResourceId, VkBuffer> m_buffers;
	};

	/// PassBuilder is used in addPass setup to declare read/write resources and usage (for barriers + ordering).
	class PassBuilder
	{
	public:
		/// Declares a resource as read by this pass with the given usage (default SampledRead).
		PassBuilder& read(ResourceId id, ImageUsage usage = ImageUsage::SampledRead);

		/// Declares a resource as written by this pass with the given usage (default ColorWrite).
		PassBuilder& write(ResourceId id, ImageUsage usage = ImageUsage::ColorWrite);

		using ReadWriteList = std::vector<std::pair<ResourceId, ImageUsage>>;
		const ReadWriteList& getReads() const { return m_reads; }
		const ReadWriteList& getWrites() const { return m_writes; }

	private:
		ReadWriteList m_reads;
		ReadWriteList m_writes;
	};

	/// Returns the layout/stage/access for the given image usage (target state for barrier).
	ResourceUsageState GetUsageState(ImageUsage usage);

	/// Execute callback: receives command buffer and registry to record commands.
	using PassExecuteFn = std::function<void(VkCommandBuffer cmd, Registry& registry)>;

	/// Setup callback: receives PassBuilder to declare read/write resources.
	using PassSetupFn = std::function<void(PassBuilder&)>;

	/// Minimal Frame Graph: logical resources (images/buffers), passes with read/write, registry resolution.
	/// Dependency graph (writer→reader) is built in compile(); passes run in topological order. Transient resources have frame lifetime.
	class FrameGraph
	{
	public:
		FrameGraph() = default;
		FrameGraph(const FrameGraph&) = delete;
		FrameGraph& operator=(const FrameGraph&) = delete;

		/// Creates a logical image resource (FG allocates VkImage per frame slot when executing).
		/// \return ResourceId for use in PassBuilder and Registry.
		ResourceId createImage(std::string_view name, const ImageDesc& desc);

		/// Creates a logical buffer resource (FG allocates VkBuffer per frame slot when executing).
		/// \return ResourceId for use in PassBuilder and Registry.
		ResourceId createBuffer(std::string_view name, const BufferDesc& desc);

		/// Registers an external image resource (no FG allocation; bind each frame via Registry::bindImage).
		/// \return ResourceId for use in PassBuilder and Registry.
		ResourceId createExternalImage(std::string_view name);

		/// Adds a pass with setup (declare reads/writes) and execute (record commands using registry).
		void addPass(std::string_view name, PassSetupFn setup, PassExecuteFn execute);

		/// Builds the dependency graph (writer→reader), detects multi-writer (assert in MVP) and cycles (LOG_FATAL),
		/// then computes topological order. Call before execute() or let execute() compile on first run.
		void compile();

		/// Executes all passes in compiled topological order: ensures transient resources exist for the frame slot,
		/// fills the registry, then runs each pass execute(cmd, registry). Compiles the graph on first run if needed.
		/// \param vmaAllocator Centralised GPU allocator (VMA); cast to VmaAllocator in implementation.
		/// \param framesInFlight Number of frame slots (e.g. 2).
		/// \param sync2Supported When true, use vkCmdPipelineBarrier2; otherwise use legacy vkCmdPipelineBarrier.
		void execute(VkDevice device, VkPhysicalDevice physicalDevice, void* vmaAllocator, VkCommandBuffer cmd,
			Registry& registry, uint32_t frameIndex, VkExtent2D extent, uint32_t framesInFlight, bool sync2Supported = true,
			engine::core::Profiler* profiler = nullptr);

		/// Releases all allocated Vulkan resources (call on shutdown or before resize).
		void destroy(VkDevice device, void* vmaAllocator);

	private:
		struct ImageResource
		{
			ResourceId id = kInvalidResourceId;
			std::string name;
			ImageDesc desc;
			bool external = false;
		};
		struct BufferResource
		{
			ResourceId id = kInvalidResourceId;
			std::string name;
			BufferDesc desc;
			bool external = false;
		};
		struct PerFrameImageHandles
		{
			VkImage image = VK_NULL_HANDLE;
			void* allocation = nullptr; ///< VmaAllocation (opaque to avoid including vk_mem_alloc.h here).
			VkImageView view = VK_NULL_HANDLE;
			bool allocatedWithVma = false;
		};
		struct PerFrameBufferHandles
		{
			VkBuffer buffer = VK_NULL_HANDLE;
			void* allocation = nullptr; ///< VmaAllocation (opaque to avoid including vk_mem_alloc.h here).
		};
		struct Pass
		{
			std::string name;
			PassBuilder::ReadWriteList reads;
			PassBuilder::ReadWriteList writes;
			PassSetupFn setup;
			PassExecuteFn execute;
		};

		void emitBarriersBeforePass(VkCommandBuffer cmd, const Pass& pass, Registry& registry,
			std::unordered_map<ResourceId, ResourceUsageState>& lastUsage, bool sync2Supported, VkDevice device);

		ResourceId m_nextId = 1;
		std::vector<ImageResource> m_imageResources;
		std::vector<BufferResource> m_bufferResources;
		std::vector<Pass> m_passes;
		std::vector<std::vector<PerFrameImageHandles>> m_perFrameImageHandles;
		std::vector<std::vector<PerFrameBufferHandles>> m_perFrameBufferHandles;
		VkExtent2D m_lastExtent = { 0, 0 };
		uint32_t m_lastFramesInFlight = 0;

		std::vector<size_t> m_compiledOrder;
		bool m_compiled = false;
		bool m_loggedImageVmaBypass = false;

		void ensureImageResources(VkDevice device, VkPhysicalDevice physicalDevice, void* vmaAllocator,
			uint32_t frameIndex, VkExtent2D extent, uint32_t framesInFlight);
		void ensureBufferResources(VkDevice device, void* vmaAllocator,
			uint32_t frameIndex, uint32_t framesInFlight);
		void fillRegistry(Registry& registry, uint32_t frameIndex);
	};
}

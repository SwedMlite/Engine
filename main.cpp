#define VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL 0
#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL_main.h>
#include <vulkan/vulkan_raii.hpp>
#include <optional>

static SDL_Window* window = NULL;

struct Frame {
	vk::raii::CommandBuffer commandBuffer;
	vk::raii::Semaphore imageAvailableSemaphore;
	vk::raii::Semaphore renderFinishedSemaphore;
	vk::raii::Fence fence;
};

constexpr uint32_t IN_FLIGHT_FRAME_COUNT{ 2 };

struct VulkanState {
	std::optional<vk::raii::Context> context{};
	std::optional<vk::raii::Instance> instance;
	std::optional<vk::raii::SurfaceKHR> surface{};
	std::optional<vk::raii::PhysicalDevice> physicalDevice{};
	uint32_t graphicsQueueFamilyIndex{};
	std::optional<vk::raii::Device> device;
	std::optional<vk::raii::Queue> graphicsQueue{};

	std::optional<vk::raii::CommandPool> commandPool{};
	std::array<std::optional<Frame>, IN_FLIGHT_FRAME_COUNT> frames{};
	uint32_t frameIndex{};

	std::optional<vk::raii::SwapchainKHR> swapchain;
	std::vector<vk::Image> swapchainImages;
	vk::Extent2D swapchainExtent;
	uint32_t currentSwapchainImageIndex;

};

void InitFrames(VulkanState* vulkanState) {
	vk::CommandBufferAllocateInfo allocInfo{};
	allocInfo.commandPool = *vulkanState->commandPool;
	allocInfo.level = vk::CommandBufferLevel::ePrimary;
	allocInfo.commandBufferCount = IN_FLIGHT_FRAME_COUNT;

	auto commandBuffers = vulkanState->device->allocateCommandBuffers(allocInfo);

	for (uint32_t i = 0; i < IN_FLIGHT_FRAME_COUNT; ++i) {
		vk::FenceCreateInfo fenceInfo{ vk::FenceCreateFlagBits::eSignaled };
		vk::SemaphoreCreateInfo semaphoreInfo{};

		vulkanState->frames[i].emplace(
			std::move(commandBuffers[i]),
			vk::raii::Semaphore(*vulkanState->device, semaphoreInfo),
			vk::raii::Semaphore(*vulkanState->device, semaphoreInfo),
			vk::raii::Fence(*vulkanState->device, fenceInfo)
		);
	}
}

void InitSurface(VulkanState* vulkanState) {
	VkSurfaceKHR raw_surface;
	if (!SDL_Vulkan_CreateSurface(window, **vulkanState->instance, nullptr, &raw_surface)) {
		SDL_Log("Failed to create Vulkan surface: %s", SDL_GetError());
	}
	vulkanState->surface.emplace(*vulkanState->instance, raw_surface);
}

void InitCommandPool(VulkanState* vulkanState) {
	vk::CommandPoolCreateInfo commandPoolCreateInfo{};
	commandPoolCreateInfo.queueFamilyIndex = vulkanState->graphicsQueueFamilyIndex;
	commandPoolCreateInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

	vulkanState->commandPool.emplace(*vulkanState->device, commandPoolCreateInfo);
}

void InitDevice(VulkanState* vulkanState) {
	vk::DeviceQueueCreateInfo queueCreateInfo{};
	queueCreateInfo.queueFamilyIndex = vulkanState->graphicsQueueFamilyIndex;
	std::array queuePriorities{ 1.0f };
	queueCreateInfo.setQueuePriorities(queuePriorities);

	vk::DeviceCreateInfo deviceCreateInfo{};
	std::array queueCreateInfos{ queueCreateInfo };
	deviceCreateInfo.setQueueCreateInfos(queueCreateInfos);
	std::array<const char*, 1> enabledExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	deviceCreateInfo.setPEnabledExtensionNames(enabledExtensions);

	vulkanState->device.emplace(*vulkanState->physicalDevice, deviceCreateInfo);

	vulkanState->graphicsQueue.emplace(*vulkanState->device, vulkanState->graphicsQueueFamilyIndex, 0);
}

void PickPhysicalDevice(VulkanState* vulkanState) {
	auto physicalDevices{ vulkanState->instance->enumeratePhysicalDevices() };
	if (physicalDevices.empty()) {
		SDL_Log("No Vulkan devices found: %s", SDL_GetError());
	}
	vulkanState->physicalDevice.emplace(*vulkanState->instance, *physicalDevices.front());
	SDL_Log("GPU: %s", vulkanState->physicalDevice->getProperties().deviceName);
	vulkanState->graphicsQueueFamilyIndex = 0;
}

void InitInstance(VulkanState* vulkanState) {

	vk::ApplicationInfo applicationInfo{};
	applicationInfo.apiVersion = vk::makeApiVersion(0, 1, 1, 0);

	vk::InstanceCreateInfo instanceCreateInfo;
	instanceCreateInfo.pApplicationInfo = &applicationInfo;
	uint32_t extensionCount;
	instanceCreateInfo.ppEnabledExtensionNames = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
	instanceCreateInfo.enabledExtensionCount = extensionCount;

	vulkanState->instance.emplace(*vulkanState->context, instanceCreateInfo);
}

void RecreateSwapchain(VulkanState* vulkanState) {

	vk::SurfaceCapabilitiesKHR surfaceCapabilities{ vulkanState->physicalDevice->getSurfaceCapabilitiesKHR(*vulkanState->surface) };
	vulkanState->swapchainExtent = surfaceCapabilities.currentExtent;

	vk::SwapchainCreateInfoKHR swapchainCreateInfo{};
	swapchainCreateInfo.surface = *vulkanState->surface;
	swapchainCreateInfo.minImageCount = surfaceCapabilities.minImageCount + 1;

	std::vector<vk::SurfaceFormatKHR> surfaceFormats =
		vulkanState->physicalDevice->getSurfaceFormatsKHR(*vulkanState->surface);
	vk::SurfaceFormatKHR selectedFormat = surfaceFormats[0];

	for (const auto& format : surfaceFormats) {
		if (format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
			selectedFormat = format;
			break;
		}
	}

	swapchainCreateInfo.imageFormat = selectedFormat.format;
	swapchainCreateInfo.imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
	swapchainCreateInfo.imageExtent = vulkanState->swapchainExtent;
	swapchainCreateInfo.imageArrayLayers = 1;
	swapchainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
	swapchainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
	swapchainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	swapchainCreateInfo.presentMode = vk::PresentModeKHR::eImmediate;
	swapchainCreateInfo.clipped = true;
	swapchainCreateInfo.oldSwapchain = nullptr;

	vulkanState->swapchain.emplace(*vulkanState->device, swapchainCreateInfo);
	vulkanState->swapchainImages = vulkanState->swapchain->getImages();
}

void TransitionImageLayout(
	vk::raii::CommandBuffer const& cmd,
	vk::Image image,
	vk::ImageLayout oldLayout,
	vk::AccessFlags srcAccess,
	vk::PipelineStageFlags srcStage,
	vk::ImageLayout newLayout,
	vk::AccessFlags dstAccess,
	vk::PipelineStageFlags dstStage
) {
	vk::ImageMemoryBarrier barrier{
		srcAccess, dstAccess,
		oldLayout, newLayout,
		VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
		image,
		vk::ImageSubresourceRange{
			vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1
		}
	};
	cmd.pipelineBarrier(
		srcStage, dstStage,
		{}, {}, {}, barrier
	);
}

void RecordCommandBuffer(vk::raii::CommandBuffer const& commandBuffer, vk::Image const& swapchainImage) {
	commandBuffer.reset();

	vk::CommandBufferBeginInfo beginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit };
	commandBuffer.begin(beginInfo);

	double t = SDL_GetTicks() * 0.001;
	vk::ClearColorValue clearColor{ std::array<float, 4>{
		static_cast<float>(SDL_sin(t * 5.0) * 0.5 + 0.5), 0.f, 0.f, 1.f } };

	TransitionImageLayout(commandBuffer, swapchainImage,
		vk::ImageLayout::eUndefined, {}, vk::PipelineStageFlagBits::eTopOfPipe,
		vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTransfer
	);

	commandBuffer.clearColorImage(
		swapchainImage, vk::ImageLayout::eTransferDstOptimal,
		clearColor,
		vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 });

	TransitionImageLayout(commandBuffer, swapchainImage,
		vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTransfer,
		vk::ImageLayout::ePresentSrcKHR, vk::AccessFlagBits::eMemoryRead, vk::PipelineStageFlagBits::eBottomOfPipe
	);

	commandBuffer.end();
}

void BeginFrame(VulkanState* vulkanState, Frame& frame) {
	vulkanState->device->waitForFences(*frame.fence, VK_TRUE, UINT64_MAX);
	vulkanState->device->resetFences(*frame.fence);

	auto [acquireResult, imageIndex] = vulkanState->swapchain->
		acquireNextImage(UINT64_MAX, *frame.imageAvailableSemaphore, nullptr);

	vulkanState->currentSwapchainImageIndex = imageIndex;
}

void EndFrame(VulkanState* vulkanState, Frame& frame) {
	vk::PresentInfoKHR presentInfo{};
	presentInfo.setSwapchains(**vulkanState->swapchain);
	presentInfo.setImageIndices(vulkanState->currentSwapchainImageIndex);
	presentInfo.setWaitSemaphores(*frame.renderFinishedSemaphore);
	vulkanState->graphicsQueue->presentKHR(presentInfo);

	vulkanState->frameIndex = (vulkanState->frameIndex + 1) % IN_FLIGHT_FRAME_COUNT;
}

void SubmitCommandBuffer(VulkanState* vulkanState, Frame& frame) {
	vk::SubmitInfo submitInfo{};
	submitInfo.setCommandBuffers(*frame.commandBuffer);
	submitInfo.setWaitSemaphores(*frame.imageAvailableSemaphore);
	submitInfo.setSignalSemaphores(*frame.renderFinishedSemaphore);
	vk::PipelineStageFlags waitStage{ vk::PipelineStageFlagBits::eTransfer };
	submitInfo.setWaitDstStageMask(waitStage);

	vulkanState->graphicsQueue->submit(submitInfo, *frame.fence);
}

void Render(VulkanState* vulkanState) {
	auto& frame = *vulkanState->frames[vulkanState->frameIndex];
	BeginFrame(vulkanState, frame);
	RecordCommandBuffer(frame.commandBuffer, vulkanState->swapchainImages[vulkanState->currentSwapchainImageIndex]);
	SubmitCommandBuffer(vulkanState, frame);
	EndFrame(vulkanState, frame);
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	if (!SDL_Vulkan_LoadLibrary(nullptr)) {
		SDL_Log("Failed to load Vulkan library: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	window = SDL_CreateWindow("Engine", 800, 600, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	if (!window) {
		SDL_Log("Failed to create SDL window: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	auto vulkanState = new VulkanState();
	*appstate = vulkanState;

	auto vkGetInstanceProcAddr{ reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr()) };
	vulkanState->context.emplace(vkGetInstanceProcAddr);

	auto vulkanVersion{ vulkanState->context->enumerateInstanceVersion() };
	SDL_Log("Vulkan: %d.%d.%d", VK_API_VERSION_MAJOR(vulkanVersion), VK_API_VERSION_MINOR(vulkanVersion), VK_API_VERSION_PATCH(vulkanVersion));

	InitInstance(vulkanState);
	InitSurface(vulkanState);
	PickPhysicalDevice(vulkanState);
	InitDevice(vulkanState);
	InitCommandPool(vulkanState);
	InitFrames(vulkanState);
	RecreateSwapchain(vulkanState);

	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
	if (event->type == SDL_EVENT_QUIT) {
		return SDL_APP_SUCCESS;
	}
	if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
		RecreateSwapchain(static_cast<VulkanState*>(appstate));
	}
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
	Render(static_cast<VulkanState*>(appstate));
	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
	auto* vulkanState = static_cast<VulkanState*>(appstate);

	if (vulkanState->device) {
		vulkanState->device->waitIdle();
	}

	delete vulkanState;

	SDL_Vulkan_UnloadLibrary();
	SDL_Quit();
}
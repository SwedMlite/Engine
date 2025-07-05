#define VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL 0
#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL_main.h>
#include <vulkan/vulkan_raii.hpp>
#include <optional>
static SDL_Window* window = NULL;
constexpr auto VULKAN_VERSION{ vk::makeApiVersion(0, 1, 1, 0) };
struct VulkanState {
	std::optional<vk::raii::Context> context{};
	std::optional<vk::raii::Instance> instance;
	std::optional<vk::raii::SurfaceKHR> surface{};
	std::optional<vk::raii::PhysicalDevice> physicalDevice{};
	uint32_t graphicsQueueFamilyIndex{};
	std::optional<vk::raii::Device> device;
	std::optional<vk::raii::Queue> graphicsQueue{};
	
	std::optional<vk::raii::CommandPool> commandPool{};
	std::vector<vk::raii::CommandBuffer> commandBuffers{};
	std::vector<vk::raii::Semaphore> imageAvailableSemaphores{};
	std::vector<vk::raii::Semaphore> renderFinishedSemaphores{};
	std::vector<vk::raii::Fence> fences{};

	std::optional<vk::raii::SwapchainKHR> swapchain;
	std::vector<vk::Image> swapchainImages;
	vk::Extent2D swapchainExtent;
	vk::Format swapchainImageFormat{ vk::Format::eB8G8R8A8Srgb };
	uint32_t currentSwapchainImageIndex;

};

void InitSurface(VulkanState* vulkanState) {
	VkSurfaceKHR raw_surface;
	if (!SDL_Vulkan_CreateSurface(window, **vulkanState->instance, nullptr, &raw_surface)) {
		SDL_Log("Failed to create Vulkan surface: %s", SDL_GetError());
	}
	vulkanState->surface.emplace(*vulkanState->instance, raw_surface);
}

void InitSyncObjects(VulkanState* vulkanState) {
	vk::FenceCreateInfo fenceCreateInfo{};
	fenceCreateInfo.flags = vk::FenceCreateFlagBits::eSignaled;

	vulkanState->fences.emplace_back(*vulkanState->device, fenceCreateInfo);

	vk::SemaphoreCreateInfo semaphoreCreateInfo{};
	vulkanState->imageAvailableSemaphores.emplace_back(*vulkanState->device, semaphoreCreateInfo);
	vulkanState->renderFinishedSemaphores.emplace_back(*vulkanState->device, semaphoreCreateInfo);
}

void AllocateCommandBuffers(VulkanState* vulkanState) {
	vk::CommandBufferAllocateInfo allocateInfo{};
	allocateInfo.commandPool = *vulkanState->commandPool;
	allocateInfo.commandBufferCount = 1;
	vulkanState->commandBuffers = vulkanState->device->allocateCommandBuffers(allocateInfo);

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
	applicationInfo.applicationVersion = VULKAN_VERSION;

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

	const std::vector<vk::Format> preferedSurfaceFormats = {
		vk::Format::eR8G8B8A8Unorm,
		vk::Format::eB8G8R8A8Unorm,
	};

	std::vector<vk::SurfaceFormatKHR> surfaceFormats = vulkanState->physicalDevice->getSurfaceFormatsKHR(*vulkanState->surface);
	vk::Format selectedFormat = surfaceFormats[0].format;
	for (auto& format : surfaceFormats)
	{
		for (int i = 0; i < preferedSurfaceFormats.size(); i++)
		{
			selectedFormat = surfaceFormats[i].format;
		}
	}

	swapchainCreateInfo.imageFormat = selectedFormat;
	swapchainCreateInfo.imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
	swapchainCreateInfo.imageExtent = vulkanState->swapchainExtent;
	swapchainCreateInfo.imageArrayLayers = 1;
	swapchainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
	swapchainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
	swapchainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	swapchainCreateInfo.presentMode = vk::PresentModeKHR::eMailbox;
	swapchainCreateInfo.clipped = true;

	// todo: figure out why old swapchain handle is invalid
	// if (swapchain.has_value()) swapchainCreateInfo.oldSwapchain = **swapchain;

	vulkanState->swapchain.emplace(*vulkanState->device, swapchainCreateInfo);
	vulkanState->swapchainImages = vulkanState->swapchain->getImages();
}

void Render(VulkanState* vulkanState) {
	vk::Fence const fence(*vulkanState->fences[0]);
	auto _ = vulkanState->device->waitForFences(fence,VK_TRUE,UINT64_MAX);
	vulkanState->device->resetFences(fence);

	auto [acquireResult,imageIndex] = vulkanState->swapchain->acquireNextImage(UINT64_MAX,vulkanState->imageAvailableSemaphores[0], nullptr);
	vulkanState->currentSwapchainImageIndex = imageIndex;

	auto const& swapchainImage{ vulkanState->swapchainImages[imageIndex] };

	vk::raii::CommandBuffer const &commandBuffer{ vulkanState->commandBuffers[0] };
	commandBuffer.reset();
	vk::CommandBufferBeginInfo beginInfo{};
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
	commandBuffer.begin(beginInfo);

	auto const time{ static_cast<double>(SDL_GetTicks()) * 0.001 };

	vk::ClearColorValue const color{
		std::array{static_cast<float>(std::sin(time * 5.0) * 0.5 + 0.5),0.0f,0.0f,1.0f}};

	// transfer image layout to transfer destination
	vk::ImageMemoryBarrier const barrier{
		vk::AccessFlagBits::eMemoryRead,vk::AccessFlagBits::eTransferWrite,
		vk::ImageLayout::eUndefined,vk::ImageLayout::eTransferDstOptimal,
		VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,
		swapchainImage,vk::ImageSubresourceRange{
			vk::ImageAspectFlagBits::eColor,0,1,0,1
	}
	};

	commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlags{}, nullptr,
		nullptr, barrier);


	commandBuffer.clearColorImage(swapchainImage, vk::ImageLayout::eTransferDstOptimal,
		color,
		vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1,0,1});

	vk::ImageMemoryBarrier const barrier2{
		vk::AccessFlagBits::eTransferWrite,vk::AccessFlagBits::eMemoryRead,
		vk::ImageLayout::eTransferDstOptimal,vk::ImageLayout::ePresentSrcKHR,
		VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,
		swapchainImage,vk::ImageSubresourceRange{
			vk::ImageAspectFlagBits::eColor,0,1,0,1
	}
	};

	commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlags{}, nullptr,
		nullptr, barrier2);

	commandBuffer.end();

	vk::SubmitInfo submitInfo{};
	submitInfo.setCommandBuffers(*commandBuffer);
	submitInfo.setWaitSemaphores(*vulkanState->imageAvailableSemaphores[0]);
	submitInfo.setSignalSemaphores(*vulkanState->renderFinishedSemaphores[0]);

	constexpr vk::PipelineStageFlags waitStage{ vk::PipelineStageFlagBits::eTransfer };
	submitInfo.setWaitDstStageMask(waitStage);

	vulkanState->graphicsQueue->submit(submitInfo,fence);

	vk::PresentInfoKHR presentInfo{};
	presentInfo.setSwapchains(**vulkanState->swapchain);
	presentInfo.setImageIndices(vulkanState->currentSwapchainImageIndex);
	presentInfo.setWaitSemaphores(*vulkanState->renderFinishedSemaphores[0]);
	_ = vulkanState->graphicsQueue->presentKHR(presentInfo);
}

/* This function runs once at startup. */
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
	SDL_Log("Vulkan: %d.%d.%d", VK_VERSION_MAJOR(vulkanVersion), VK_VERSION_MINOR(vulkanVersion), VK_VERSION_PATCH(vulkanVersion));
	
	InitInstance(vulkanState);
	InitSurface(vulkanState);
	PickPhysicalDevice(vulkanState);
	InitDevice(vulkanState);
	InitCommandPool(vulkanState);
	AllocateCommandBuffers(vulkanState);
	InitSyncObjects(vulkanState);
	RecreateSwapchain(vulkanState);

	return SDL_APP_CONTINUE;
}
/* This function runs when a new event (mouse input, keypresses, etc) occurs. */
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
	auto* vulkanState = static_cast<VulkanState*>(appstate);

	if (event->type == SDL_EVENT_QUIT) {
		return SDL_APP_SUCCESS;  /* end the program, reporting success to the OS. */
	}
	if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
		RecreateSwapchain(vulkanState);
	}
	return SDL_APP_CONTINUE;
}
/* This function runs once per frame, and is the heart of the program. */
SDL_AppResult SDL_AppIterate(void* appstate)
{
	auto* vulkanState = static_cast<VulkanState*>(appstate);

	Render(vulkanState);
	return SDL_APP_CONTINUE;  /* carry on with the program! */
}
/* This function runs once at shutdown. */
void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
	auto* vulkanState = static_cast<VulkanState*>(appstate);

	vulkanState->device->waitIdle();
	SDL_Vulkan_UnloadLibrary();
	SDL_Quit();
}
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

inline constexpr uint32_t IN_FLIGHT_FRAME_COUNT = 2;

struct VulkanState {
	std::optional<vk::raii::Context> context;
	std::optional<vk::raii::Instance> instance;
	std::optional<vk::raii::SurfaceKHR> surface;
	std::optional<vk::raii::PhysicalDevice> physicalDevice;
	uint32_t graphicsQueueFamilyIndex;
	std::optional<vk::raii::Device> device;
	std::optional<vk::raii::Queue> graphicsQueue;

	std::optional<vk::raii::CommandPool> commandPool;
	std::array<std::optional<Frame>, IN_FLIGHT_FRAME_COUNT> frames;
	uint32_t frameIndex;

	std::optional<vk::raii::SwapchainKHR> swapchain;
	std::vector<vk::Image> swapchainImages;
	vk::Format swapchainImageFormat;
	vk::Extent2D swapchainExtent;
	std::vector<vk::raii::ImageView> swapchainImageViews;
	std::vector<vk::raii::Framebuffer> swapchainFramebuffers;

	std::optional<vk::raii::RenderPass> renderPass;
	std::optional<vk::raii::PipelineLayout> pipelineLayout;
	std::optional<vk::raii::Pipeline> graphicsPipeline;

	uint32_t currentSwapchainImageIndex;
};

void InitFrames(VulkanState* vulkanState) {
	vk::CommandBufferAllocateInfo allocInfo;
	allocInfo.commandPool = *vulkanState->commandPool;
	allocInfo.level = vk::CommandBufferLevel::ePrimary;
	allocInfo.commandBufferCount = IN_FLIGHT_FRAME_COUNT;

	auto commandBuffers = vulkanState->device->allocateCommandBuffers(allocInfo);

	for (uint32_t i = 0; i < IN_FLIGHT_FRAME_COUNT; ++i) {
		vk::FenceCreateInfo fenceInfo{ vk::FenceCreateFlagBits::eSignaled };
		vk::SemaphoreCreateInfo semaphoreInfo;

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
	vk::CommandPoolCreateInfo commandPoolCreateInfo;
	commandPoolCreateInfo.queueFamilyIndex = vulkanState->graphicsQueueFamilyIndex;
	commandPoolCreateInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

	vulkanState->commandPool.emplace(*vulkanState->device, commandPoolCreateInfo);
}

void InitDevice(VulkanState* vulkanState) {
	vk::DeviceQueueCreateInfo queueCreateInfo;
	queueCreateInfo.queueFamilyIndex = vulkanState->graphicsQueueFamilyIndex;
	std::array queuePriorities{ 1.0f };
	queueCreateInfo.setQueuePriorities(queuePriorities);

	vk::DeviceCreateInfo deviceCreateInfo;
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
		SDL_Log("No Vulkan devices found");
	}
	vulkanState->physicalDevice.emplace(*vulkanState->instance, *physicalDevices.front());
	SDL_Log("GPU: %s", vulkanState->physicalDevice->getProperties().deviceName.data());
	vulkanState->graphicsQueueFamilyIndex = 0;
}

void InitInstance(VulkanState* vulkanState) {
	vk::ApplicationInfo applicationInfo;
	applicationInfo.apiVersion = vk::makeApiVersion(0, 1, 1, 0);

	vk::InstanceCreateInfo instanceCreateInfo;
	instanceCreateInfo.pApplicationInfo = &applicationInfo;
	uint32_t extensionCount;
	instanceCreateInfo.ppEnabledExtensionNames = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
	instanceCreateInfo.enabledExtensionCount = extensionCount;

	vulkanState->instance.emplace(*vulkanState->context, instanceCreateInfo);
}

bool IsWindowMinimized() {
	int width = 0, height = 0;
	SDL_GetWindowSizeInPixels(window, &width, &height);
	return width == 0 || height == 0;
}

void RecreateSwapchain(VulkanState* vulkanState) {

	vk::SurfaceCapabilitiesKHR surfaceCapabilities{ vulkanState->physicalDevice->getSurfaceCapabilitiesKHR(*vulkanState->surface) };
	vulkanState->swapchainExtent = surfaceCapabilities.currentExtent;

	vk::SwapchainCreateInfoKHR swapchainCreateInfo;
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

	vulkanState->swapchainImageFormat = selectedFormat.format;

	swapchainCreateInfo.imageFormat = vulkanState->swapchainImageFormat;
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

void RecordCommandBuffer(VulkanState* vulkanState, vk::raii::CommandBuffer const& commandBuffer, uint32_t imageIndex) {
	commandBuffer.reset();

	vk::CommandBufferBeginInfo beginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit };
	commandBuffer.begin(beginInfo);

	vk::RenderPassBeginInfo renderPassInfo;
	renderPassInfo.renderPass = *vulkanState->renderPass;
	renderPassInfo.framebuffer = *vulkanState->swapchainFramebuffers[imageIndex];
	renderPassInfo.renderArea.offset.x = 0;
	renderPassInfo.renderArea.offset.y = 0;
	renderPassInfo.renderArea.extent = vulkanState->swapchainExtent;

	auto t = SDL_GetTicks() * 0.001;
	vk::ClearValue clearColor;
	clearColor.color = vk::ClearColorValue{
		std::array<float, 4>{
			static_cast<float>(SDL_sin(t * 5.0) * 0.5 + 0.5), 0.0f, 0.0f, 1.0f
		}
	};
	renderPassInfo.setClearValues(clearColor);

	commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *vulkanState->graphicsPipeline);

	vk::Viewport viewport;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(vulkanState->swapchainExtent.width);
	viewport.height = static_cast<float>(vulkanState->swapchainExtent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	commandBuffer.setViewport(0, viewport);

	vk::Rect2D scissor;
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent = vulkanState->swapchainExtent;
	commandBuffer.setScissor(0, scissor);

	commandBuffer.draw(3, 1, 0, 0);

	commandBuffer.endRenderPass();
	commandBuffer.end();
}

void CreateImageViews(VulkanState* vulkanState) {
	vulkanState->swapchainImageViews.clear();
	vulkanState->swapchainImageViews.reserve(vulkanState->swapchainImages.size());

	for (const auto& image : vulkanState->swapchainImages) {
		vk::ImageViewCreateInfo imageViewCreateInfo;
		imageViewCreateInfo.image = image;
		imageViewCreateInfo.viewType = vk::ImageViewType::e2D;
		imageViewCreateInfo.format = vulkanState->swapchainImageFormat;
		imageViewCreateInfo.components = {
			vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
			vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity
		};
		imageViewCreateInfo.subresourceRange = {
			vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1
		};

		vulkanState->swapchainImageViews.emplace_back(
			*vulkanState->device, imageViewCreateInfo
		);
	}
}

void CreateFramebuffers(VulkanState* state) {
	state->swapchainFramebuffers.clear();
	state->swapchainFramebuffers.reserve(state->swapchainImageViews.size());

	for (const auto& imageView : state->swapchainImageViews) {
		vk::ImageView attachments[] = { *imageView };

		vk::FramebufferCreateInfo framebufferInfo{
			{},
			*state->renderPass,
			1,
			attachments,
			state->swapchainExtent.width,
			state->swapchainExtent.height,
			1
		};

		state->swapchainFramebuffers.emplace_back(
			*state->device, framebufferInfo
		);
	}
}

bool RecreateSwapchainSafe(VulkanState* state) {
	vk::SurfaceCapabilitiesKHR caps = state->physicalDevice->getSurfaceCapabilitiesKHR(*state->surface);

	if (IsWindowMinimized() || caps.currentExtent.width == 0 || caps.currentExtent.height == 0) {
		return false;
	}

	state->device->waitIdle();
	try {
		RecreateSwapchain(state);
		CreateImageViews(state);
		CreateFramebuffers(state);
		return true;
	}
	catch (const std::exception& e) {
		SDL_Log("Failed to recreate swapchain: %s", e.what());
		return false;
	}
}

void BeginFrame(VulkanState* vulkanState, Frame& frame) {
	vulkanState->device->waitForFences(*frame.fence, VK_TRUE, UINT64_MAX);
	vulkanState->device->resetFences(*frame.fence);

	auto [acquireResult, imageIndex] = vulkanState->swapchain->
		acquireNextImage(UINT64_MAX, *frame.imageAvailableSemaphore, nullptr);

	vulkanState->currentSwapchainImageIndex = imageIndex;
}

void EndFrame(VulkanState* vulkanState, Frame& frame) {
	vk::PresentInfoKHR presentInfo;
	presentInfo.setSwapchains(**vulkanState->swapchain);
	presentInfo.setImageIndices(vulkanState->currentSwapchainImageIndex);
	presentInfo.setWaitSemaphores(*frame.renderFinishedSemaphore);
	vulkanState->graphicsQueue->presentKHR(presentInfo);

	vulkanState->frameIndex = (vulkanState->frameIndex + 1) % IN_FLIGHT_FRAME_COUNT;
}

void SubmitCommandBuffer(VulkanState* vulkanState, Frame& frame) {
	vk::SubmitInfo submitInfo;
	submitInfo.setCommandBuffers(*frame.commandBuffer);
	submitInfo.setWaitSemaphores(*frame.imageAvailableSemaphore);
	submitInfo.setSignalSemaphores(*frame.renderFinishedSemaphore);

	vk::PipelineStageFlags waitStage{ vk::PipelineStageFlagBits::eColorAttachmentOutput };
	submitInfo.setWaitDstStageMask(waitStage);

	vulkanState->graphicsQueue->submit(submitInfo, *frame.fence);
}

void Render(VulkanState* state) {
	auto& frame = *state->frames[state->frameIndex];
	BeginFrame(state, frame);

	RecordCommandBuffer(state, frame.commandBuffer, state->currentSwapchainImageIndex);
	SubmitCommandBuffer(state, frame);
	EndFrame(state, frame);
}

static std::vector<char> readFile(const char* filename) {
	size_t fileSize = 0;
	void* data = SDL_LoadFile(filename, &fileSize);
	if (!data) {
		SDL_Log("Failed to load file. %s", SDL_GetError());
	}
	std::vector<char> buffer(static_cast<char*>(data), static_cast<char*>(data) + fileSize);
	return buffer;
}

vk::raii::ShaderModule CreateShaderModule(VulkanState* vulkanState, const std::vector<char>& code) {
	vk::ShaderModuleCreateInfo shaderModuleCreateInfo;
	shaderModuleCreateInfo.codeSize = code.size();
	shaderModuleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

	return vk::raii::ShaderModule(*vulkanState->device, shaderModuleCreateInfo);
}

void CreateGraphicsPipeline(VulkanState* vulkanState) {
	auto vertShaderCode = readFile("shaders/vert.spv");
	auto fragShaderCode = readFile("shaders/frag.spv");

	auto vertShaderModule = CreateShaderModule(vulkanState, vertShaderCode);
	auto fragShaderModule = CreateShaderModule(vulkanState, fragShaderCode);

	vk::PipelineShaderStageCreateInfo vertShaderStageInfo;
	vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
	vertShaderStageInfo.module = *vertShaderModule;
	vertShaderStageInfo.pName = "main";

	vk::PipelineShaderStageCreateInfo fragShaderStageInfo;
	fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
	fragShaderStageInfo.module = *fragShaderModule;
	fragShaderStageInfo.pName = "main";

	vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

	vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
	vertexInputInfo.vertexBindingDescriptionCount = 0;
	vertexInputInfo.vertexAttributeDescriptionCount = 0;

	vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
	inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
	inputAssembly.primitiveRestartEnable = vk::False;

	vk::PipelineViewportStateCreateInfo viewportState;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	vk::PipelineRasterizationStateCreateInfo rasterizer;
	rasterizer.depthClampEnable = vk::False;
	rasterizer.rasterizerDiscardEnable = vk::False;
	rasterizer.polygonMode = vk::PolygonMode::eFill;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = vk::CullModeFlagBits::eBack;
	rasterizer.frontFace = vk::FrontFace::eClockwise;
	rasterizer.depthBiasEnable = vk::False;

	vk::PipelineMultisampleStateCreateInfo multisampling;
	multisampling.sampleShadingEnable = vk::False;
	multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

	vk::PipelineColorBlendAttachmentState colorBlendAttachment;
	colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	colorBlendAttachment.blendEnable = vk::False;

	vk::PipelineColorBlendStateCreateInfo colorBlending;
	colorBlending.logicOpEnable = vk::False;
	colorBlending.logicOp = vk::LogicOp::eCopy;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;
	colorBlending.blendConstants[0] = 0.0f;
	colorBlending.blendConstants[1] = 0.0f;
	colorBlending.blendConstants[2] = 0.0f;
	colorBlending.blendConstants[3] = 0.0f;

	std::vector<vk::DynamicState> dynamicStates = {
		vk::DynamicState::eViewport,
		vk::DynamicState::eScissor,
	};

	vk::PipelineDynamicStateCreateInfo dynamicState;
	dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
	dynamicState.pDynamicStates = dynamicStates.data();

	vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
	pipelineLayoutInfo.setLayoutCount = 0;
	pipelineLayoutInfo.pushConstantRangeCount = 0;

	vulkanState->pipelineLayout.emplace(*vulkanState->device, pipelineLayoutInfo);

	vk::GraphicsPipelineCreateInfo pipelineInfo;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = *vulkanState->pipelineLayout;
	pipelineInfo.renderPass = *vulkanState->renderPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	vulkanState->graphicsPipeline.emplace(*vulkanState->device, nullptr, pipelineInfo);
}

void CreateRenderPass(VulkanState* vulkanState) {
	vk::AttachmentDescription colorAttachment;
	colorAttachment.format = vulkanState->swapchainImageFormat;
	colorAttachment.samples = vk::SampleCountFlagBits::e1;
	colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
	colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
	colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
	colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

	vk::AttachmentReference colorAttachmentRef;
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

	vk::SubpassDescription subpass;
	subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;

	vk::RenderPassCreateInfo renderPassInfo;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;

	vulkanState->renderPass.emplace(*vulkanState->device, renderPassInfo);
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
	SDL_Log("Vulkan: %d.%d.%d", vk::apiVersionMajor(vulkanVersion), vk::apiVersionMinor(vulkanVersion), vk::apiVersionPatch(vulkanVersion));

	InitInstance(vulkanState);
	InitSurface(vulkanState);
	PickPhysicalDevice(vulkanState);
	InitDevice(vulkanState);
	InitCommandPool(vulkanState);
	InitFrames(vulkanState);
	RecreateSwapchain(vulkanState);
	CreateImageViews(vulkanState);
	CreateRenderPass(vulkanState);
	CreateGraphicsPipeline(vulkanState);
	CreateFramebuffers(vulkanState);

	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
	if (event->type == SDL_EVENT_QUIT) {
		return SDL_APP_SUCCESS;
	}
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
	auto* state = static_cast<VulkanState*>(appstate);

	if (IsWindowMinimized()) {
		return SDL_APP_CONTINUE;
	}

	vk::SurfaceCapabilitiesKHR caps = state->physicalDevice->getSurfaceCapabilitiesKHR(*state->surface);
	if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0) {
		return SDL_APP_CONTINUE;
	}

	if (!state->swapchain || !state->device || state->swapchainExtent != caps.currentExtent) {
		RecreateSwapchainSafe(state);
	}

	Render(state);
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
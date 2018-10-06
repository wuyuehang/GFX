#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <cassert>
#include <fstream>
#include <string>

using std::string;
using std::ifstream;
using std::vector;

class KanVul {
public:
    ~KanVul() {
        vkQueueWaitIdle(_queue);

        vkFreeMemory(_dev, _vertexBufMem, nullptr);
        vkDestroyBuffer(_dev, _vertexBuf, nullptr);

        for (const auto iter : _fb) {
            vkDestroyFramebuffer(_dev, iter, nullptr);
        }

        vkDestroyPipelineLayout(_dev, _layout, nullptr);
        vkDestroyPipeline(_dev, _gfxPipeline, nullptr);
        vkDestroyRenderPass(_dev, _renderpass, nullptr);

        vkDestroySemaphore(_dev, _swpImgAcquire, nullptr);
        vkDestroySemaphore(_dev, _renderImgFinished, nullptr);
        vkFreeCommandBuffers(_dev, _cmdpool, _cmdbuf.size(), _cmdbuf.data());
        vkDestroyCommandPool(_dev, _cmdpool, nullptr);

        for (const auto iter : _swpchain_imgv) {
            vkDestroyImageView(_dev, iter, nullptr);
        }
        vkDestroySwapchainKHR(_dev, _swpchain, nullptr);
        vkDestroyDevice(_dev, nullptr);
        vkDestroySurfaceKHR(_inst, _surf, nullptr);
        vkDestroyInstance(_inst, nullptr);

        glfwDestroyWindow(_glfw);
        glfwTerminate();
    }

    KanVul() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        _glfw = glfwCreateWindow(800, 800, "KanVul_gfx_template", nullptr, nullptr);

        InitInstance();
        glfwCreateWindowSurface(_inst, _glfw, nullptr, &_surf);
        InitPhysicalDevice();
        InitDevice();
        InitSwapchain();
        InitCmdPool();
        InitCmdBuffers();
        InitVertexBuffers();
        InitSyncObj();
        InitRenderPass();
        InitFixedFuncGFXPipeline();
        InitGFXPipeline();
        InitFramebuffers();
    }

    /* Find a memory in `memoryTypeBitsRequirement` that includes all of `requiredProperties`
     * this function is copied from vulkan spec */
    int32_t findProperties(const VkPhysicalDeviceMemoryProperties* pMemoryProperties,
        uint32_t memoryTypeBitsRequirement,
        VkMemoryPropertyFlags requiredProperties) {
        const uint32_t memoryCount = pMemoryProperties->memoryTypeCount;

        for (uint32_t memoryIndex = 0; memoryIndex < memoryCount; ++memoryIndex) {
            const uint32_t memoryTypeBits = (1 << memoryIndex);
            const bool isRequiredMemoryType = memoryTypeBitsRequirement & memoryTypeBits;

            const VkMemoryPropertyFlags properties = pMemoryProperties->memoryTypes[memoryIndex].propertyFlags;
            const bool hasRequiredProperties = (properties & requiredProperties) == requiredProperties;

            if (isRequiredMemoryType && hasRequiredProperties)
                return static_cast<int32_t>(memoryIndex);
        }
        return -1;
    }

    vector<char> loadSPIRV(const string& filename) {
        ifstream f(filename, std::ios::ate | std::ios::binary);
        size_t filesize = (size_t) f.tellg();
        vector<char> buffer(filesize);
        f.seekg(0);
        f.read(buffer.data(), filesize);
        f.close();

        return buffer;
    }

    void Run() {
        /* record begin command buffer */
        VkCommandBufferBeginInfo cbi {};
        cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbi.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        for (uint8_t i = 0; i < _cmdbuf.size(); i++) {
            vkBeginCommandBuffer(_cmdbuf[i], &cbi);

            VkRenderPassBeginInfo rpBeginInfo {};
            rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpBeginInfo.renderPass = _renderpass;
            rpBeginInfo.framebuffer = _fb[i];
            rpBeginInfo.renderArea.offset = {0, 0};
            rpBeginInfo.renderArea.extent = _surfcapkhr.currentExtent;
            rpBeginInfo.clearValueCount = 1;
            VkClearValue cv {};
            cv.color = {0.25, 0.0, 0.0, 1.0};
            rpBeginInfo.pClearValues = &cv;
            vkCmdBeginRenderPass(_cmdbuf[i], &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(_cmdbuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS, _gfxPipeline);
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(_cmdbuf[i], 0, 1, &_vertexBuf, &offset);
            vkCmdDraw(_cmdbuf[i], 3, 1, 0, 0);
            vkCmdEndRenderPass(_cmdbuf[i]);

            vkEndCommandBuffer(_cmdbuf[i]);
        }

        uint32_t ImageIndex = 0;
        while (!glfwWindowShouldClose(_glfw)) {
            glfwPollEvents();

            /* presentation engine will block forever until unused images are available */
            vkAcquireNextImageKHR(_dev, _swpchain, UINT64_MAX, _swpImgAcquire, VK_NULL_HANDLE, &ImageIndex);

            VkSubmitInfo si {};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            /* stages prior to output color stage can already process while output color stage must
             * wait until _swpImgAcquire semaphore is signaled.
             */
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores = &_swpImgAcquire;
            VkPipelineStageFlags ws = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            si.pWaitDstStageMask = &ws;

            si.commandBufferCount = 1;
            si.pCommandBuffers = &_cmdbuf[ImageIndex];
            /* signal finish of render process, status from unsignaled to signaled */
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores = &_renderImgFinished;

            vkQueueSubmit(_queue, 1, &si, VK_NULL_HANDLE);

            VkPresentInfoKHR pi {};
            pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            pi.pNext = nullptr;
            /* presentation engine can only start present image until the render executions onto the
             * image have been finished (_renderImgFinished semaphore signaled)
             */
            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = &_renderImgFinished;
            pi.swapchainCount = 1;
            pi.pSwapchains = &_swpchain;
            pi.pImageIndices = &ImageIndex;
            pi.pResults = nullptr;

            vkQueuePresentKHR(_queue, &pi);
        }
    }

    void InitInstance() {
        VkInstanceCreateInfo instInfo {};
        instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instInfo.pApplicationInfo = nullptr;

        vector<const char *> ie;
        ie.push_back("VK_KHR_surface");
        ie.push_back("VK_KHR_xcb_surface");
        instInfo.enabledExtensionCount = ie.size();
        instInfo.ppEnabledExtensionNames = ie.data();
        vkCreateInstance(&instInfo, nullptr, &_inst);
    }

    void InitPhysicalDevice() {
        uint32_t physicalDeviceCount = 0;
        vkEnumeratePhysicalDevices(_inst, &physicalDeviceCount, nullptr);
        _pdev.resize(physicalDeviceCount);
        vkEnumeratePhysicalDevices(_inst, &physicalDeviceCount, _pdev.data());

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(_pdev[0], &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamily;
        queueFamily.resize(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(_pdev[0], &queueFamilyCount, queueFamily.data());

        uint32_t fmtc = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(_pdev[0], _surf, &fmtc, nullptr);
        _surffmtkhr.resize(fmtc);
        vkGetPhysicalDeviceSurfaceFormatsKHR(_pdev[0], _surf, &fmtc, _surffmtkhr.data());
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_pdev[0], _surf, &_surfcapkhr);
        vkGetPhysicalDeviceMemoryProperties(_pdev[0], &_pdmp);
    }

    void InitDevice() {
        VkDeviceQueueCreateInfo queueInfo {};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = 0;
        queueInfo.queueCount = 1;
        float priority = 1.0;
        queueInfo.pQueuePriorities = &priority;

        VkDeviceCreateInfo deviceInfo {};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;

        vector<const char *> de;
        de.push_back("VK_KHR_swapchain");
        deviceInfo.enabledExtensionCount = de.size();
        deviceInfo.ppEnabledExtensionNames = de.data();

        vkCreateDevice(_pdev[0], &deviceInfo, nullptr, &_dev);
        VkBool32 presentSupported = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(_pdev[0], 0, _surf, &presentSupported);
        assert(VK_TRUE == presentSupported);

        vkGetDeviceQueue(_dev, 0, 0, &_queue);
    }

    void InitSwapchain() {
        VkSwapchainCreateInfoKHR swapchainInfo {};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.surface = _surf;
        swapchainInfo.minImageCount = _surfcapkhr.minImageCount;
        swapchainInfo.imageFormat = _surffmtkhr[0].format;
        swapchainInfo.imageColorSpace = _surffmtkhr[0].colorSpace;
        swapchainInfo.imageExtent = _surfcapkhr.currentExtent;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainInfo.preTransform = _surfcapkhr.currentTransform;
        swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapchainInfo.clipped = VK_TRUE;
        swapchainInfo.oldSwapchain = VK_NULL_HANDLE;

        vkCreateSwapchainKHR(_dev, &swapchainInfo, nullptr, &_swpchain);

        uint32_t scimgc = 0;
        vkGetSwapchainImagesKHR(_dev, _swpchain, &scimgc, nullptr);
        _swpchain_img.resize(scimgc);
        _swpchain_imgv.resize(scimgc);
        vkGetSwapchainImagesKHR(_dev, _swpchain, &scimgc, _swpchain_img.data());

        for (uint32_t i = 0; i < scimgc; i++) {
            VkImageViewCreateInfo imgvi {};
            imgvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imgvi.image = _swpchain_img[i];
            imgvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imgvi.format = _surffmtkhr[0].format;
            imgvi.components.r = VK_COMPONENT_SWIZZLE_R;
            imgvi.components.g = VK_COMPONENT_SWIZZLE_G;
            imgvi.components.b = VK_COMPONENT_SWIZZLE_B;
            imgvi.components.a = VK_COMPONENT_SWIZZLE_A;
            imgvi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imgvi.subresourceRange.baseMipLevel = 0;
            imgvi.subresourceRange.levelCount = 1;
            imgvi.subresourceRange.baseArrayLayer = 0;
            imgvi.subresourceRange.layerCount = 1;

            vkCreateImageView(_dev, &imgvi, nullptr, &_swpchain_imgv[i]);
        }
    }

    void InitCmdPool() {
        VkCommandPoolCreateInfo cmdPoolInfo {};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cmdPoolInfo.queueFamilyIndex = 0;
        vkCreateCommandPool(_dev, &cmdPoolInfo, nullptr, &_cmdpool);
    }

    void InitCmdBuffers() {
        VkCommandBufferAllocateInfo cmdBufInfo {};
        cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufInfo.commandPool = _cmdpool;
        cmdBufInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufInfo.commandBufferCount = _swpchain_img.size();
        _cmdbuf.resize(_swpchain_img.size());
        vkAllocateCommandBuffers(_dev, &cmdBufInfo, _cmdbuf.data());
    }

    void InitVertexBuffers() {
        GLfloat position[] = {
            -1.0, -1.0, 0.0, 1.0,
            0.0, 1.0, 0.0, 1.0,
            1.0, -1.0, 0.0, 1.0
        };

        VkBufferCreateInfo posbufInfo {};
        posbufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        posbufInfo.size = sizeof(position);
        posbufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        posbufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(_dev, &posbufInfo, nullptr, &_vertexBuf);

        VkMemoryRequirements req {};
        vkGetBufferMemoryRequirements(_dev, _vertexBuf, &req);

        int32_t memoryType = findProperties(&_pdmp, req.memoryTypeBits, req.memoryTypeBits);
        assert(memoryType != -1);

        VkMemoryAllocateInfo allocBufInfo {};
        allocBufInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocBufInfo.allocationSize = req.size;
        allocBufInfo.memoryTypeIndex = memoryType;

        vkAllocateMemory(_dev, &allocBufInfo, nullptr, &_vertexBufMem);

        uint8_t *pDST = nullptr;
        vkMapMemory(_dev, _vertexBufMem, 0, req.size, 0, (void **)&pDST);

        uint8_t *pSRC = (uint8_t *)((const void *)&position[0]);
        for (int32_t i = 0; i < req.size; i++) {
            *(pDST + i) = *(pSRC + i);
        }

        vkUnmapMemory(_dev, _vertexBufMem);
        vkBindBufferMemory(_dev, _vertexBuf, _vertexBufMem, 0);
    }

    void InitSyncObj() {
        VkSemaphoreCreateInfo semaInfo {};
        semaInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore(_dev, &semaInfo, nullptr, &_swpImgAcquire);
        vkCreateSemaphore(_dev, &semaInfo, nullptr, &_renderImgFinished);
    }

    void InitRenderPass() {
        VkAttachmentDescription colorAttDesc {};
        colorAttDesc.flags = 0;
        colorAttDesc.format = _surffmtkhr[0].format;
        colorAttDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttDesc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttRef {};
        colorAttRef.attachment = 0;
        colorAttRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription spDesc {};
        spDesc.flags = 0;
        spDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        spDesc.inputAttachmentCount = 0;
        spDesc.pInputAttachments = nullptr;
        spDesc.colorAttachmentCount = 1;
        spDesc.pColorAttachments = &colorAttRef;
        spDesc.pResolveAttachments = nullptr;
        spDesc.pDepthStencilAttachment = nullptr;
        spDesc.preserveAttachmentCount = 0;
        spDesc.pPreserveAttachments = nullptr;

        VkSubpassDependency subpassDep {};
        subpassDep.srcSubpass = VK_SUBPASS_EXTERNAL;
        subpassDep.dstSubpass = 0;
        subpassDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep.srcAccessMask = 0;
        subpassDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo {};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.pNext = nullptr;
        rpInfo.flags = 0;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttDesc;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &spDesc;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &subpassDep;

        vkCreateRenderPass(_dev, &rpInfo, nullptr, &_renderpass);
    }

    void InitFixedFuncGFXPipeline() {
        /* fixed function viewport scissor state */
        __scissor = {
            .offset = { 0, 0 },
            .extent = _surfcapkhr.currentExtent };

        __vp = { 0, 0, float(_surfcapkhr.currentExtent.width), float(_surfcapkhr.currentExtent.height), 0, 1};

        __vpsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        __vpsInfo.viewportCount = 1;
        __vpsInfo.pViewports = &__vp;
        __vpsInfo.scissorCount = 1;
        __vpsInfo.pScissors = &__scissor;

        /* fixed function rasterization state */
        __rstInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        __rstInfo.depthClampEnable = VK_FALSE;
        __rstInfo.rasterizerDiscardEnable = VK_FALSE;
        __rstInfo.polygonMode = VK_POLYGON_MODE_FILL;
        __rstInfo.cullMode = VK_CULL_MODE_NONE;
        __rstInfo.depthBiasEnable = VK_FALSE;
        __rstInfo.lineWidth = 1.0;

        /* fixed function MSAA state */
        __msaaInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        __msaaInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        __msaaInfo.sampleShadingEnable = VK_FALSE;

        /* fixed function blend state */
        __colorBldAttaState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        __colorBldAttaState.blendEnable = VK_FALSE;

        __bldInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        __bldInfo.logicOpEnable = VK_FALSE;
        __bldInfo.attachmentCount = 1;
        __bldInfo.pAttachments = &__colorBldAttaState;
    }

    void InitGFXPipeline() {
        /* graphics pipeline -- shader */
        VkShaderModule vertShaderModule = VK_NULL_HANDLE;
        auto vert = loadSPIRV("single_attribute.vert.spv");
        VkShaderModuleCreateInfo vertShaderModuleInfo {};
        vertShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vertShaderModuleInfo.codeSize = vert.size();
        vertShaderModuleInfo.pCode = (const uint32_t *)vert.data();
        vkCreateShaderModule(_dev, &vertShaderModuleInfo, nullptr, &vertShaderModule);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo {};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkShaderModule fragShaderModule = VK_NULL_HANDLE;
        auto frag = loadSPIRV("constant.frag.spv");
        VkShaderModuleCreateInfo fragShaderModuleInfo {};
        fragShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        fragShaderModuleInfo.codeSize = frag.size();
        fragShaderModuleInfo.pCode = (const uint32_t *)frag.data();
        vkCreateShaderModule(_dev, &fragShaderModuleInfo, nullptr, &fragShaderModule);

        VkPipelineShaderStageCreateInfo fragShaderStageInfo {};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStageInfos[2] = { vertShaderStageInfo, fragShaderStageInfo };

        /* graphics pipeline -- state */
        VkVertexInputBindingDescription vbd {};
        vbd.binding = 0;
        vbd.stride = 4*sizeof(float);
        vbd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription ad {};
        ad.location = 0;
        ad.binding = 0;
        ad.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        ad.offset = 0;

        VkPipelineVertexInputStateCreateInfo vertInputInfo {};
        vertInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertInputInfo.vertexBindingDescriptionCount = 1;
        vertInputInfo.pVertexBindingDescriptions = &vbd;
        vertInputInfo.vertexAttributeDescriptionCount = 1;
        vertInputInfo.pVertexAttributeDescriptions = &ad;

        VkPipelineInputAssemblyStateCreateInfo iaInfo {};
        iaInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        iaInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        iaInfo.primitiveRestartEnable = VK_FALSE;

        VkPipelineLayoutCreateInfo layoutInfo {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pNext = nullptr;
        layoutInfo.pNext = 0;
        layoutInfo.setLayoutCount = 0;
        layoutInfo.pSetLayouts = nullptr;
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = nullptr;
        vkCreatePipelineLayout(_dev, &layoutInfo, nullptr, &_layout);

        VkGraphicsPipelineCreateInfo gfxPipelineInfo {};
        gfxPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gfxPipelineInfo.pNext = nullptr;
        gfxPipelineInfo.flags = 0;
        gfxPipelineInfo.stageCount = 2;
        gfxPipelineInfo.pStages = shaderStageInfos;
        gfxPipelineInfo.pVertexInputState = &vertInputInfo;
        gfxPipelineInfo.pInputAssemblyState = &iaInfo;
        gfxPipelineInfo.pTessellationState = nullptr;
        gfxPipelineInfo.pViewportState = &__vpsInfo;
        gfxPipelineInfo.pRasterizationState = &__rstInfo;
        gfxPipelineInfo.pMultisampleState = &__msaaInfo;
        gfxPipelineInfo.pDepthStencilState = nullptr;
        gfxPipelineInfo.pColorBlendState = &__bldInfo;
        gfxPipelineInfo.pDynamicState = nullptr;
        gfxPipelineInfo.layout = _layout;
        gfxPipelineInfo.renderPass = _renderpass;
        gfxPipelineInfo.subpass = 0;
        gfxPipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        gfxPipelineInfo.basePipelineIndex = -1;

        vkCreateGraphicsPipelines(_dev, VK_NULL_HANDLE, 1, &gfxPipelineInfo, nullptr, &_gfxPipeline);
        vkDestroyShaderModule(_dev, vertShaderModule, nullptr);
        vkDestroyShaderModule(_dev, fragShaderModule, nullptr);
    }

    void InitFramebuffers() {
        _fb.resize(_swpchain_img.size());

        for (uint32_t i = 0; i < _swpchain_img.size(); i++) {
            VkFramebufferCreateInfo fbInfo {};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = _renderpass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &_swpchain_imgv[i];
            fbInfo.width = _surfcapkhr.currentExtent.width;
            fbInfo.height = _surfcapkhr.currentExtent.height;
            fbInfo.layers = 1;
            vkCreateFramebuffer(_dev, &fbInfo, nullptr, &_fb[i]);
        }
    }

private:
    GLFWwindow *_glfw;
    VkInstance _inst;
    VkSurfaceKHR _surf;
    vector<VkPhysicalDevice> _pdev;
    VkPhysicalDeviceMemoryProperties _pdmp {};
    VkDevice _dev;
    VkSurfaceCapabilitiesKHR _surfcapkhr;
    std::vector<VkSurfaceFormatKHR> _surffmtkhr;
    VkSwapchainKHR _swpchain;
    vector<VkImage> _swpchain_img;
    vector<VkImageView> _swpchain_imgv;
    VkCommandPool _cmdpool;
    vector<VkCommandBuffer> _cmdbuf;
    VkQueue _queue;
    /* sync between presentation engine and start of command execution */
    VkSemaphore _swpImgAcquire;
    /* sync between finish of command execution and present request to presentation engine */
    VkSemaphore _renderImgFinished;
    VkRenderPass _renderpass;
    VkPipelineLayout _layout;
    VkPipeline _gfxPipeline;
    vector<VkFramebuffer> _fb;
    /* fixed function pipeline */
    VkPipelineViewportStateCreateInfo __vpsInfo {};
    VkViewport __vp {};
    VkRect2D __scissor {};
    VkPipelineRasterizationStateCreateInfo __rstInfo {};
    VkPipelineMultisampleStateCreateInfo __msaaInfo {};
    VkPipelineColorBlendStateCreateInfo __bldInfo {};
    VkPipelineColorBlendAttachmentState __colorBldAttaState {};
    /* resource */
    VkBuffer _vertexBuf;
    VkDeviceMemory _vertexBufMem;
};

int main(int argc, char const *argv[])
{
    KanVul app;
    app.Run();

    return 0;
}

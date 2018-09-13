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

        for (const auto iter : _fb) {
            vkDestroyFramebuffer(_dev, iter, nullptr);
        }

        vkDestroyPipelineLayout(_dev, _layout, nullptr);
        vkDestroyPipeline(_dev, _gfxPipeline, nullptr);
        vkDestroyRenderPass(_dev, _renderpass, nullptr);

        vkDestroySemaphore(_dev, _swpImgAcquire, nullptr);
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
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        _glfw = glfwCreateWindow(800, 800, "KanVul_draw", nullptr, nullptr);
        glfwShowWindow(_glfw);

        InitInstance();
        glfwCreateWindowSurface(_inst, _glfw, nullptr, &_surf);
        InitPhysicalDevice();
        InitDevice();
        InitSwapchain();
        InitCmdPool();
        InitCmdBuffers();
        InitSyncObj();
        InitRenderPass();
        InitGFXPipeline();
        InitFramebuffers();
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
        cbi.pNext = nullptr;
        cbi.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        cbi.pInheritanceInfo = nullptr;

        for (uint8_t i = 0; i < _cmdbuf.size(); i++) {
            vkBeginCommandBuffer(_cmdbuf[i], &cbi);

            VkRenderPassBeginInfo rpBeginInfo {};
            rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpBeginInfo.pNext = nullptr;
            rpBeginInfo.renderPass = _renderpass;
            rpBeginInfo.framebuffer = _fb[i];
            rpBeginInfo.renderArea.offset = {0, 0};
            rpBeginInfo.renderArea.extent = {800, 800};
            rpBeginInfo.clearValueCount = 1;
            VkClearValue cv {};
            cv.color = {0.25, 0.0, 0.0, 1.0};
            rpBeginInfo.pClearValues = &cv;
            vkCmdBeginRenderPass(_cmdbuf[i], &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(_cmdbuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS, _gfxPipeline);
            vkCmdDraw(_cmdbuf[i], 3, 1, 0, 0);
            vkCmdEndRenderPass(_cmdbuf[i]);

            vkEndCommandBuffer(_cmdbuf[i]);
        }

        uint32_t ImageIndex = 0;
        while (!glfwWindowShouldClose(_glfw)) {
            glfwPollEvents();

            vkAcquireNextImageKHR(_dev, _swpchain, UINT64_MAX, _swpImgAcquire, VK_NULL_HANDLE, &ImageIndex);

            VkSubmitInfo si {};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.pNext = nullptr;
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores = &_swpImgAcquire;
            VkPipelineStageFlags ws = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            si.pWaitDstStageMask = &ws;

            si.commandBufferCount = 1;
            si.pCommandBuffers = &_cmdbuf[ImageIndex];
            si.signalSemaphoreCount = 0;
            si.pSignalSemaphores = nullptr;

            vkQueueSubmit(_queue, 1, &si, VK_NULL_HANDLE);

            VkPresentInfoKHR pi {};
            pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            pi.pNext = nullptr;
            pi.waitSemaphoreCount = 0;
            pi.pWaitSemaphores = nullptr;
            pi.swapchainCount = 1;
            pi.pSwapchains = &_swpchain;
            pi.pImageIndices = &ImageIndex;
            pi.pResults = nullptr;

            vkQueuePresentKHR(_queue, &pi);
        }
    }

    void InitInstance() {
        VkApplicationInfo ai {};
        ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pApplicationName = "KanVul_draw";
        ai.pEngineName = "KanVul_draw";
        ai.apiVersion = VK_API_VERSION_1_1;

        VkInstanceCreateInfo ii {};
        ii.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ii.pApplicationInfo = &ai;

        vector<const char *> ie;
        ie.push_back("VK_KHR_surface");
        ie.push_back("VK_KHR_xcb_surface");
        ii.enabledExtensionCount = ie.size();
        ii.ppEnabledExtensionNames = ie.data();

        vkCreateInstance(&ii, nullptr, &_inst);
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
    }

    void InitDevice() {
        VkDeviceQueueCreateInfo qi {};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = 0;
        qi.queueCount = 1;
        float priority = 1.0;
        qi.pQueuePriorities = &priority;

        VkDeviceCreateInfo di {};
        di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        di.queueCreateInfoCount = 1;
        di.pQueueCreateInfos = &qi;

        vector<const char *> de;
        de.push_back("VK_KHR_swapchain");
        di.enabledExtensionCount = de.size();
        di.ppEnabledExtensionNames = de.data();
        di.pEnabledFeatures = nullptr;

        vkCreateDevice(_pdev[0], &di, nullptr, &_dev);
        VkBool32 presentSupported = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(_pdev[0], 0, _surf, &presentSupported);
        assert(VK_TRUE == presentSupported);

        vkGetDeviceQueue(_dev, 0, 0, &_queue);
    }

    void InitSwapchain() {
        uint32_t fmtc = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(_pdev[0], _surf, &fmtc, nullptr);
        std::vector<VkSurfaceFormatKHR> fmt;
        fmt.resize(fmtc);
        vkGetPhysicalDeviceSurfaceFormatsKHR(_pdev[0], _surf, &fmtc, fmt.data());

        VkSurfaceCapabilitiesKHR cap;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_pdev[0], _surf, &cap);

        VkSwapchainCreateInfoKHR sci;
        sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        sci.pNext = nullptr;
        sci.flags = 0;
        sci.surface = _surf;
        sci.minImageCount = cap.minImageCount;
        sci.imageFormat = fmt[0].format;
        sci.imageColorSpace = fmt[0].colorSpace;
        sci.imageExtent = cap.currentExtent;
        sci.imageArrayLayers = 1;
        sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sci.queueFamilyIndexCount = 0;
        sci.pQueueFamilyIndices = nullptr;
        sci.preTransform = cap.currentTransform;
        sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        sci.clipped = VK_TRUE;
        sci.oldSwapchain = VK_NULL_HANDLE;

        vkCreateSwapchainKHR(_dev, &sci, nullptr, &_swpchain);

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
            imgvi.format = fmt[0].format;
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
        VkCommandPoolCreateInfo ci {};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.pNext = nullptr;
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = 0;

        vkCreateCommandPool(_dev, &ci, nullptr, &_cmdpool);
    }

    void InitCmdBuffers() {
        VkCommandBufferAllocateInfo bi {};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        bi.pNext = nullptr;
        bi.commandPool = _cmdpool;
        bi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        bi.commandBufferCount = _swpchain_img.size();

        _cmdbuf.resize(_swpchain_img.size());
        vkAllocateCommandBuffers(_dev, &bi, _cmdbuf.data());
    }

    void InitSyncObj() {
        VkSemaphoreCreateInfo swpImgAcquireSemaInfo {};
        swpImgAcquireSemaInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore(_dev, &swpImgAcquireSemaInfo, nullptr, &_swpImgAcquire);
    }

    void InitRenderPass() {
        VkAttachmentDescription colorAttDesc {};
        colorAttDesc.flags = 0;
        colorAttDesc.format = VK_FORMAT_B8G8R8A8_SRGB;
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

    void InitGFXPipeline() {
        /* graphics pipeline -- shader */
        VkShaderModule vertShaderModule = VK_NULL_HANDLE;
        auto vert = loadSPIRV("kanvul_draw.vert.spv");
        VkShaderModuleCreateInfo vertShaderModuleInfo {};
        vertShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vertShaderModuleInfo.pNext = nullptr;
        vertShaderModuleInfo.flags = 0;
        vertShaderModuleInfo.codeSize = vert.size();
        vertShaderModuleInfo.pCode = (const uint32_t *)vert.data();
        vkCreateShaderModule(_dev, &vertShaderModuleInfo, nullptr, &vertShaderModule);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo {};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.pNext = nullptr;
        vertShaderStageInfo.flags = 0;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";
        vertShaderStageInfo.pSpecializationInfo = nullptr;

        VkShaderModule fragShaderModule = VK_NULL_HANDLE;
        auto frag = loadSPIRV("kanvul_draw.frag.spv");
        VkShaderModuleCreateInfo fragShaderModuleInfo {};
        fragShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        fragShaderModuleInfo.pNext = nullptr;
        fragShaderModuleInfo.flags = 0;
        fragShaderModuleInfo.codeSize = frag.size();
        fragShaderModuleInfo.pCode = (const uint32_t *)frag.data();
        vkCreateShaderModule(_dev, &fragShaderModuleInfo, nullptr, &fragShaderModule);

        VkPipelineShaderStageCreateInfo fragShaderStageInfo {};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.pNext = nullptr;
        fragShaderStageInfo.flags = 0;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";
        fragShaderStageInfo.pSpecializationInfo = nullptr;

        VkPipelineShaderStageCreateInfo shaderStageInfos[2] = { vertShaderStageInfo, fragShaderStageInfo };

        /* graphics pipeline -- state */
        VkPipelineVertexInputStateCreateInfo vertInputInfo {};
        vertInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo iaInfo {};
        iaInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        iaInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        iaInfo.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo vpInfo {};
        vpInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vpInfo.viewportCount = 1;
        VkViewport vp { 0, 0, 800, 800, 0, 1};
        vpInfo.pViewports = &vp;
        vpInfo.scissorCount = 1;
        VkRect2D scissor {};
        scissor.offset.x = 0;
        scissor.offset.y = 0;
        scissor.extent.width = 800;
        scissor.extent.height = 800;
        vpInfo.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rstInfo {};
        rstInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rstInfo.depthClampEnable = VK_FALSE;
        rstInfo.rasterizerDiscardEnable = VK_FALSE;
        rstInfo.polygonMode = VK_POLYGON_MODE_FILL;
        rstInfo.cullMode = VK_CULL_MODE_NONE;
        rstInfo.depthBiasEnable = VK_FALSE;
        rstInfo.lineWidth = 1.0;

        VkPipelineMultisampleStateCreateInfo msaaInfo {};
        msaaInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        msaaInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        msaaInfo.sampleShadingEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBldAttaState = {};
        colorBldAttaState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBldAttaState.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo bldInfo {};
        bldInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        bldInfo.logicOpEnable = VK_FALSE;
        bldInfo.attachmentCount = 1;
        bldInfo.pAttachments = &colorBldAttaState;

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
        gfxPipelineInfo.pViewportState = &vpInfo;
        gfxPipelineInfo.pRasterizationState = &rstInfo;
        gfxPipelineInfo.pMultisampleState = &msaaInfo;
        gfxPipelineInfo.pDepthStencilState = nullptr;
        gfxPipelineInfo.pColorBlendState = &bldInfo;
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
            fbInfo.pNext = nullptr;
            fbInfo.flags = 0;
            fbInfo.renderPass = _renderpass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &_swpchain_imgv[i];
            fbInfo.width = 800;
            fbInfo.height = 800;
            fbInfo.layers = 1;

            vkCreateFramebuffer(_dev, &fbInfo, nullptr, &_fb[i]);
        }
    }

private:
    GLFWwindow *_glfw;
    VkInstance _inst;
    VkSurfaceKHR _surf;
    vector<VkPhysicalDevice> _pdev;
    VkDevice _dev;
    VkSwapchainKHR _swpchain;
    vector<VkImage> _swpchain_img;
    vector<VkImageView> _swpchain_imgv;
    VkCommandPool _cmdpool;
    vector<VkCommandBuffer> _cmdbuf;
    VkQueue _queue;
    VkSemaphore _swpImgAcquire;
    VkRenderPass _renderpass;
    VkPipelineLayout _layout;
    VkPipeline _gfxPipeline;
    vector<VkFramebuffer> _fb;
};

int main(int argc, char const *argv[])
{
    KanVul app;
    app.Run();

    return 0;
}

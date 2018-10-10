#ifndef _VOLCANO_HPP_
#define _VOLCANO_HPP_

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "resource_mgnt.hpp"

using std::cout;
using std::endl;
using std::ifstream;
using std::string;
using std::vector;

vector<char> loadSPIRV(const string& filename) {
    ifstream f(filename, std::ios::ate | std::ios::binary);
    size_t filesize = (size_t) f.tellg();
    vector<char> buffer(filesize);
    f.seekg(0);
    f.read(buffer.data(), filesize);
    f.close();

    return buffer;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vcDbgReportCallback(VkFlags msgFlags, VkDebugReportObjectTypeEXT objType,
    uint64_t srcObject, size_t location, int32_t msgCode,
    const char *layerPrefix, const char *msg, void *userData) {

    if (msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
        cout << "ERROR: [";
    } else if (msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
        cout << "WARNING: [";
    } else if (msgFlags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
        cout << "INFORMATION: [";
    } else if (msgFlags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
        cout << "PERFORMANCE: [";
    } else if (msgFlags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
        cout << "DEBUG: [";
    }
    cout << layerPrefix << "] Code" << msgCode << ":" << msg << endl;
    return VK_FALSE;
}

struct PSOTemplate {
    /* fixed function pipeline */
    VkPipelineViewportStateCreateInfo vpsInfo {};
    VkViewport vp {};
    VkRect2D scissor {};
    VkPipelineRasterizationStateCreateInfo rstInfo {};
    VkPipelineMultisampleStateCreateInfo msaaInfo {};
    VkPipelineDepthStencilStateCreateInfo dsInfo {};
    VkPipelineColorBlendStateCreateInfo bldInfo {};
    VkPipelineColorBlendAttachmentState colorBldAttaState {};
};

class Volcano {
public:
    ~Volcano() {
        for (const auto iter : fb) {
            vkDestroyFramebuffer(device, iter, nullptr);
        }
        vkDestroyRenderPass(device, renderpass, nullptr);

        vkDestroyImageView(device, depth_imgv, nullptr);
        vkFreeMemory(device, depth_mem, nullptr);
        vkDestroyImage(device, depth_img, nullptr);

        vkDestroySemaphore(device, swapImgAcquire, nullptr);
        vkDestroySemaphore(device, renderImgFinished, nullptr);
        vkFreeCommandBuffers(device, rendercmdpool, rendercmdbuf.size(), rendercmdbuf.data());
        vkDestroyCommandPool(device, rendercmdpool, nullptr);

        for (const auto iter : swapchain_imgv) {
            vkDestroyImageView(device, iter, nullptr);
        }

        vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyDevice(device, nullptr);
        //dbgDestroyDebugReportCallback(instance, dbg_report_cb, nullptr);
        vkDestroyInstance(instance, nullptr);

        glfwDestroyWindow(glfw);
        glfwTerminate();
    }

    Volcano() {
        _initInstance();
        //_initDBGCallback();
        _initPhysicalDevice();
        _initDevice();
        _initWSI();
        _initSwapchain();

        _initRenderCmdBuf();
        _initSyncObj();

        _bakeDepth();
        _initRenderPass();
        _initFramebuffer();

        _bakePSOTemplate();
    }

    virtual void _initInstance() final {
        VkInstanceCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

        vector<const char *> ie;
        ie.push_back("VK_KHR_surface");
        ie.push_back("VK_KHR_xcb_surface");
        ie.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        info.enabledExtensionCount = ie.size();
        info.ppEnabledExtensionNames = ie.data();

        vkCreateInstance(&info, nullptr, &instance);
    }

    virtual void _initPhysicalDevice() final {
        uint32_t cnt = 0;
        vkEnumeratePhysicalDevices(instance, &cnt, nullptr);
        phydev.resize(cnt);
        vkEnumeratePhysicalDevices(instance, &cnt, phydev.data());

        cnt = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(phydev[0], &cnt, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamily;
        queueFamily.resize(cnt);
        vkGetPhysicalDeviceQueueFamilyProperties(phydev[0], &cnt, queueFamily.data());

        vkGetPhysicalDeviceMemoryProperties(phydev[0], &pdmp);
        vkGetPhysicalDeviceProperties(phydev[0], &pdp);
    }

    virtual void _initDevice() final {
        VkDeviceQueueCreateInfo queueInfo = {};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = 0;
        queueInfo.queueCount = 1;
        float priority = 1.0;
        queueInfo.pQueuePriorities = &priority;

        VkDeviceCreateInfo deviceInfo = {};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;

        vector<const char *> de;
        de.push_back("VK_KHR_swapchain");
        deviceInfo.enabledExtensionCount = de.size();
        deviceInfo.ppEnabledExtensionNames = de.data();

        vkCreateDevice(phydev[0], &deviceInfo, nullptr, &device);
        vkGetDeviceQueue(device, 0, 0, &queue);
    }

    virtual void _initWSI() final {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfw = glfwCreateWindow(800, 800, __FILE__, nullptr, nullptr);

        glfwCreateWindowSurface(instance, glfw, nullptr, &surface);

        uint32_t fmtc = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(phydev[0], surface, &fmtc, nullptr);
        surfacefmtkhr.resize(fmtc);
        vkGetPhysicalDeviceSurfaceFormatsKHR(phydev[0], surface, &fmtc, surfacefmtkhr.data());
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phydev[0], surface, &surfacecapkhr);

        VkBool32 presentSupported = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(phydev[0], 0, surface, &presentSupported);
        assert(VK_TRUE == presentSupported);
    }

    virtual void _initDBGCallback() final {
        dbgCreateDebugReportCallback = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
        dbgDestroyDebugReportCallback = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");

        VkDebugReportCallbackCreateInfoEXT dbgCallbackInfo = {};
        dbgCallbackInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        dbgCallbackInfo.pfnCallback = vcDbgReportCallback;
        dbgCallbackInfo.flags = VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT
            | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_ERROR_BIT_EXT
            | VK_DEBUG_REPORT_DEBUG_BIT_EXT;
        dbgCallbackInfo.pUserData = this;
        dbgCreateDebugReportCallback(instance, &dbgCallbackInfo, nullptr, &dbg_report_cb);
    }

    virtual void _initSwapchain() final {
        VkSwapchainCreateInfoKHR swapchainInfo = {};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.surface = surface;
        swapchainInfo.minImageCount = surfacecapkhr.minImageCount;
        swapchainInfo.imageFormat = surfacefmtkhr[0].format;
        swapchainInfo.imageColorSpace = surfacefmtkhr[0].colorSpace;
        swapchainInfo.imageExtent = surfacecapkhr.currentExtent;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainInfo.preTransform = surfacecapkhr.currentTransform;
        swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapchainInfo.clipped = VK_TRUE;
        swapchainInfo.oldSwapchain = VK_NULL_HANDLE;

        vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain);

        uint32_t cnt = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &cnt, nullptr);
        swapchain_img.resize(cnt);
        swapchain_imgv.resize(cnt);
        vkGetSwapchainImagesKHR(device, swapchain, &cnt, swapchain_img.data());

        for (uint32_t i = 0; i < cnt; i++) {
            VkImageViewCreateInfo imgvi = {};
            imgvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imgvi.image = swapchain_img[i];
            imgvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imgvi.format = surfacefmtkhr[0].format;
            imgvi.components.r = VK_COMPONENT_SWIZZLE_R;
            imgvi.components.g = VK_COMPONENT_SWIZZLE_G;
            imgvi.components.b = VK_COMPONENT_SWIZZLE_B;
            imgvi.components.a = VK_COMPONENT_SWIZZLE_A;
            imgvi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imgvi.subresourceRange.baseMipLevel = 0;
            imgvi.subresourceRange.levelCount = 1;
            imgvi.subresourceRange.baseArrayLayer = 0;
            imgvi.subresourceRange.layerCount = 1;

            vkCreateImageView(device, &imgvi, nullptr, &swapchain_imgv[i]);
        }
    }

    virtual void _initRenderCmdBuf() final {
        VkCommandPoolCreateInfo cmdPoolInfo = {};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cmdPoolInfo.queueFamilyIndex = 0;
        vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &rendercmdpool);

        VkCommandBufferAllocateInfo cmdBufInfo = {};
        cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufInfo.commandPool = rendercmdpool;
        cmdBufInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufInfo.commandBufferCount = swapchain_img.size();
        rendercmdbuf.resize(swapchain_img.size());
        vkAllocateCommandBuffers(device, &cmdBufInfo, rendercmdbuf.data());
    }

    virtual void _initSyncObj() final {
        VkSemaphoreCreateInfo semaInfo = {};
        semaInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore(device, &semaInfo, nullptr, &swapImgAcquire);
        vkCreateSemaphore(device, &semaInfo, nullptr, &renderImgFinished);
    }

    virtual void _bakeDepth() final {
        VkImageCreateInfo dsImgInfo = {};
        dsImgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        dsImgInfo.imageType = VK_IMAGE_TYPE_2D;
        dsImgInfo.format = VK_FORMAT_D32_SFLOAT;
        dsImgInfo.extent.width = surfacecapkhr.currentExtent.width;
        dsImgInfo.extent.height = surfacecapkhr.currentExtent.height;
        dsImgInfo.extent.depth = 1;
        dsImgInfo.mipLevels = 1;
        dsImgInfo.arrayLayers = 1;
        dsImgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        dsImgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        dsImgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        dsImgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        dsImgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkCreateImage(device, &dsImgInfo, nullptr, &depth_img);

        VkMemoryRequirements req = {};
        vkGetImageMemoryRequirements(device, depth_img, &req);

        VkMemoryAllocateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        info.allocationSize = req.size;
        info.memoryTypeIndex = resource_manager.findProperties(&pdmp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device, &info, nullptr, &depth_mem);

        vkBindImageMemory(device, depth_img, depth_mem, 0);

        VkImageViewCreateInfo dsImgViewInfo = {};
        dsImgViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        dsImgViewInfo.image = depth_img;
        dsImgViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        dsImgViewInfo.format = VK_FORMAT_D32_SFLOAT;
        dsImgViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        dsImgViewInfo.subresourceRange.baseMipLevel = 0;
        dsImgViewInfo.subresourceRange.levelCount = 1;
        dsImgViewInfo.subresourceRange.baseArrayLayer = 0;
        dsImgViewInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &dsImgViewInfo, nullptr, &depth_imgv);

        /* transition depth layout */
        VkCommandBuffer transitionCMD = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo transitionCMDAllocInfo = {};
        transitionCMDAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        transitionCMDAllocInfo.commandPool = rendercmdpool;
        transitionCMDAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        transitionCMDAllocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(device, &transitionCMDAllocInfo, &transitionCMD);

        VkCommandBufferBeginInfo cbi = {};
        cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(transitionCMD, &cbi);

        VkImageMemoryBarrier imb = {};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imb.srcAccessMask = 0;
        imb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imb.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imb.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.image = depth_img;
        imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        imb.subresourceRange.baseMipLevel = 0;
        imb.subresourceRange.levelCount = 1;
        imb.subresourceRange.baseArrayLayer = 0;
        imb.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(transitionCMD, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imb);

        vkEndCommandBuffer(transitionCMD);
        VkSubmitInfo si = {};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &transitionCMD;
        vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);
        vkFreeCommandBuffers(device, rendercmdpool, 1, &transitionCMD);
    }

    virtual void _initRenderPass() final {
        VkAttachmentDescription colorAttDesc = {};
        colorAttDesc.flags = 0;
        colorAttDesc.format = surfacefmtkhr[0].format;
        colorAttDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttDesc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttRef = {};
        colorAttRef.attachment = 0;
        colorAttRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depthAttDesc = {};
        depthAttDesc.flags = 0;
        depthAttDesc.format = VK_FORMAT_D32_SFLOAT;
        depthAttDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttDesc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttRef = {};
        depthAttRef.attachment = 1;
        depthAttRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription spDesc = {};
        spDesc.flags = 0;
        spDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        spDesc.inputAttachmentCount = 0;
        spDesc.pInputAttachments = nullptr;
        spDesc.colorAttachmentCount = 1;
        spDesc.pColorAttachments = &colorAttRef;
        spDesc.pResolveAttachments = nullptr;
        spDesc.pDepthStencilAttachment = &depthAttRef;
        spDesc.preserveAttachmentCount = 0;
        spDesc.pPreserveAttachments = nullptr;

        VkSubpassDependency subpassDep = {};
        subpassDep.srcSubpass = VK_SUBPASS_EXTERNAL;
        subpassDep.dstSubpass = 0;
        subpassDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep.srcAccessMask = 0;
        subpassDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo = {};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.pNext = nullptr;
        rpInfo.flags = 0;
        rpInfo.attachmentCount = 2;
        VkAttachmentDescription attDescs[2] = { colorAttDesc, depthAttDesc };
        rpInfo.pAttachments = attDescs;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &spDesc;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &subpassDep;

        vkCreateRenderPass(device, &rpInfo, nullptr, &renderpass);
    }

    virtual void _initFramebuffer() final {
        fb.resize(swapchain_img.size());

        for (uint32_t i = 0; i < swapchain_img.size(); i++) {
            VkFramebufferCreateInfo fbInfo = {};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = renderpass;
            VkImageView att[2] = { swapchain_imgv[i], depth_imgv };
            fbInfo.attachmentCount = 2;
            fbInfo.pAttachments = att;
            fbInfo.width = surfacecapkhr.currentExtent.width;
            fbInfo.height = surfacecapkhr.currentExtent.height;
            fbInfo.layers = 1;
            vkCreateFramebuffer(device, &fbInfo, nullptr, &fb[i]);
        }
    }

    virtual void _bakePSOTemplate() final {
        /* fixed function viewport scissor state */
        fixfunc_templ.scissor = {
            .offset = { 0, 0 },
            .extent = surfacecapkhr.currentExtent };

        fixfunc_templ.vp = { 0, 0, float(surfacecapkhr.currentExtent.width),
            float(surfacecapkhr.currentExtent.height), 0, 1};

        fixfunc_templ.vpsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        fixfunc_templ.vpsInfo.viewportCount = 1;
        fixfunc_templ.vpsInfo.pViewports = &fixfunc_templ.vp;
        fixfunc_templ.vpsInfo.scissorCount = 1;
        fixfunc_templ.vpsInfo.pScissors = &fixfunc_templ.scissor;

        /* fixed function rasterization state */
        fixfunc_templ.rstInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        fixfunc_templ.rstInfo.depthClampEnable = VK_FALSE;
        fixfunc_templ.rstInfo.rasterizerDiscardEnable = VK_FALSE;
        fixfunc_templ.rstInfo.polygonMode = VK_POLYGON_MODE_FILL;
        fixfunc_templ.rstInfo.cullMode = VK_CULL_MODE_NONE;
        fixfunc_templ.rstInfo.depthBiasEnable = VK_FALSE;
        fixfunc_templ.rstInfo.lineWidth = 1.0;

        /* fixed function MSAA state */
        fixfunc_templ.msaaInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        fixfunc_templ.msaaInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        fixfunc_templ.msaaInfo.sampleShadingEnable = VK_FALSE;

        /* fixed function DS state */
        fixfunc_templ.dsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        fixfunc_templ.dsInfo.depthTestEnable = VK_TRUE;
        fixfunc_templ.dsInfo.depthWriteEnable = VK_TRUE;
        fixfunc_templ.dsInfo.depthCompareOp = VK_COMPARE_OP_LESS;
        fixfunc_templ.dsInfo.depthBoundsTestEnable = VK_FALSE;
        fixfunc_templ.dsInfo.stencilTestEnable = VK_FALSE;

        /* fixed function blend state */
        fixfunc_templ.colorBldAttaState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        fixfunc_templ.colorBldAttaState.blendEnable = VK_FALSE;

        fixfunc_templ.bldInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        fixfunc_templ.bldInfo.logicOpEnable = VK_FALSE;
        fixfunc_templ.bldInfo.attachmentCount = 1;
        fixfunc_templ.bldInfo.pAttachments = &fixfunc_templ.colorBldAttaState;
    }

    virtual void _bakePipelineCache(const string& filename, VkPipelineCache &pplcache) final {
        ifstream f(filename, std::ios::ate | std::ios::binary);
        size_t filesize = (size_t) f.tellg();
        vector<char> buffer(filesize);
        f.seekg(0);
        f.read(buffer.data(), filesize);
        f.close();

        size_t cache_size = 0;
        void *cache_datum = nullptr;

        if (buffer.size()) {
            uint32_t hdrlength = 0;
            uint32_t ppchdrver = 0;
            uint32_t vendorid = 0;
            uint32_t deviceid = 0;
            uint8_t uuid[VK_UUID_SIZE] = {};
            memcpy(&hdrlength, buffer.data(), 4);
            memcpy(&ppchdrver, buffer.data() + 4, 4);
            memcpy(&vendorid, buffer.data() + 8, 4);
            memcpy(&deviceid, buffer.data() + 12, 4);
            memcpy(uuid, buffer.data() + 16, VK_UUID_SIZE);
            VkBool32 invalidate = VK_FALSE;
            if (hdrlength > 0 && ppchdrver == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
                vendorid == pdp.vendorID && deviceid == pdp.deviceID &&
                memcmp(uuid, pdp.pipelineCacheUUID, VK_UUID_SIZE) == 0) {
                cache_size = buffer.size();
                cache_datum = buffer.data();
            }
        }

        VkPipelineCacheCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        info.initialDataSize = cache_size;
        info.pInitialData = cache_datum;

        vkCreatePipelineCache(device, &info, nullptr, &pplcache);
    }

    virtual void _diskPipelineCache(const string &filename, VkPipelineCache &pplcache) final {
        size_t cache_size = 0;
        vector<char> cache_datum;

        vkGetPipelineCacheData(device, pplcache, &cache_size, nullptr);
        cache_datum.resize(cache_size);
        vkGetPipelineCacheData(device, pplcache, &cache_size, cache_datum.data());

        std::ofstream f(filename, std::ios::out);
        f.write(cache_datum.data(), cache_size);
        f.close();
    }

    virtual void Run() final {
        glfwShowWindow(glfw);
        uint32_t ImageIndex = 0;
        while (!glfwWindowShouldClose(glfw)) {
            glfwPollEvents();

            /* presentation engine will block forever until unused images are available */
            vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, swapImgAcquire, VK_NULL_HANDLE, &ImageIndex);

            VkSubmitInfo si {};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            /* stages prior to output color stage can already process while output color stage must
             * wait until _swpImgAcquire semaphore is signaled.
             */
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores = &swapImgAcquire;
            VkPipelineStageFlags ws = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            si.pWaitDstStageMask = &ws;

            si.commandBufferCount = 1;
            si.pCommandBuffers = &rendercmdbuf[ImageIndex];
            /* signal finish of render process, status from unsignaled to signaled */
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores = &renderImgFinished;

            vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);

            VkPresentInfoKHR pi {};
            pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            pi.pNext = nullptr;
            /* presentation engine can only start present image until the render executions onto the
             * image have been finished (_renderImgFinished semaphore signaled)
             */
            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = &renderImgFinished;
            pi.swapchainCount = 1;
            pi.pSwapchains = &swapchain;
            pi.pImageIndices = &ImageIndex;
            pi.pResults = nullptr;

            vkQueuePresentKHR(queue, &pi);
        }
    }

public:
    GLFWwindow *glfw;
    VkInstance instance;
    vector<VkPhysicalDevice> phydev;
    VkPhysicalDeviceProperties pdp {};
    VkPhysicalDeviceMemoryProperties pdmp {};
    VkDevice device;
    /* for simplicity, one queue to support GFX, compute, transfer and presentation */
    VkQueue queue;
    VkSurfaceKHR surface;
    vector<VkSurfaceFormatKHR> surfacefmtkhr;
    VkSurfaceCapabilitiesKHR surfacecapkhr;
    PFN_vkCreateDebugReportCallbackEXT dbgCreateDebugReportCallback;
    PFN_vkDestroyDebugReportCallbackEXT dbgDestroyDebugReportCallback;
    VkDebugReportCallbackEXT dbg_report_cb;
    VkSwapchainKHR swapchain;
    vector<VkImage> swapchain_img;
    vector<VkImageView> swapchain_imgv;
    vector<VkCommandBuffer> rendercmdbuf;
    ResouceMgnt resource_manager;
    VkImage depth_img;
    VkDeviceMemory depth_mem;
    VkImageView depth_imgv;
    VkRenderPass renderpass;
    PSOTemplate fixfunc_templ;
    VkCommandPool rendercmdpool;
    vector<VkFramebuffer> fb;
private:
    /* sync between presentation engine and start of command execution */
    VkSemaphore swapImgAcquire;
    /* sync between finish of command execution and present request to presentation engine */
    VkSemaphore renderImgFinished;
};

#endif

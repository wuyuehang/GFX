#ifndef _VOLCANO_LITE_HPP_
#define _VOLCANO_LITE_HPP_

#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "resource_mgnt.hpp"

using std::array;
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

struct TexObj {
    VkImage img;
    VkDeviceMemory memory;
    VkImageView imgv;
};

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
    VkDynamicState dynamicState[2] {};
    VkPipelineDynamicStateCreateInfo dynamicInfo {};
};

class Volcano {
public:
    ~Volcano() {
        vkDestroyImageView(device, depth_imgv, nullptr);
        vkFreeMemory(device, depth_mem, nullptr);
        vkDestroyImage(device, depth_img, nullptr);

        vkFreeCommandBuffers(device, cmdpool, cmdbuf.size(), cmdbuf.data());
        vkDestroyCommandPool(device, cmdpool, nullptr);

        for (const auto iter : swapchain_imgv) {
            vkDestroyImageView(device, iter, nullptr);
        }

        vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);

        glfwDestroyWindow(glfw);
        glfwTerminate();
    }

    Volcano() {
        initInstance();
        initPhysicalDevice();
        initDevice();
        initWSI();
        initSwapchain();
        initCmdBuf();
        initDepth();
        initPSOTemplate();
    }

    VkShaderModule initShaderModule(const string& filename) {
        VkShaderModule module = VK_NULL_HANDLE;
        auto spirv = loadSPIRV(filename);

        VkShaderModuleCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = spirv.size();
        info.pCode = (const uint32_t *) spirv.data();

        vkCreateShaderModule(device, &info, nullptr, &module);

        return module;
    }

    void bakeImage(struct TexObj &texo, VkFormat fmt, uint32_t w, uint32_t h,
        VkImageTiling tiling, VkImageUsageFlags usage, const void *pData) {

        assert(texo.img == VK_NULL_HANDLE);
        assert(texo.imgv == VK_NULL_HANDLE);
        assert(texo.memory == VK_NULL_HANDLE);

        VkImageCreateInfo imgInfo {};
        imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.format = fmt;
        imgInfo.extent.width = w;
        imgInfo.extent.height = h;
        imgInfo.extent.depth = 1;
        imgInfo.mipLevels = 1;
        imgInfo.arrayLayers = 1;
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling = tiling;
        imgInfo.usage = usage;
        imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (pData != nullptr) {
            imgInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
        } else {
            imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        vkCreateImage(device, &imgInfo, nullptr, &texo.img);

        VkMemoryRequirements req = {};
        vkGetImageMemoryRequirements(device, texo.img, &req);

        VkMemoryAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = req.size;
        allocInfo.memoryTypeIndex = resource_manager.findProperties(&pdmp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device, &allocInfo, nullptr, &texo.memory);

        vkBindImageMemory(device, texo.img, texo.memory, 0);

        /* upload content */
        if (pData != nullptr) {
            VkImageSubresource subresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .arrayLayer = 0,
            };

            VkSubresourceLayout subresource_layout = {};
            vkGetImageSubresourceLayout(device, texo.img, &subresource, &subresource_layout);

            uint8_t *pDST = nullptr;

            vkMapMemory(device, texo.memory, 0, req.size, 0, (void **)&pDST);
            for (uint32_t i = 0; i < h; i++) {
                memcpy(pDST, ((uint8_t *)pData + w * i * 4), w * 4);
                pDST = pDST + subresource_layout.rowPitch;
            }
            vkUnmapMemory(device, texo.memory);
        }

        VkImageUsageFlags combine = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
            | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
        if (usage & combine) {
            VkImageViewCreateInfo imgViewInfo {};
            imgViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imgViewInfo.image = texo.img;
            imgViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imgViewInfo.format = fmt;
            imgViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imgViewInfo.subresourceRange.baseMipLevel = 0;
            imgViewInfo.subresourceRange.levelCount = 1;
            imgViewInfo.subresourceRange.baseArrayLayer = 0;
            imgViewInfo.subresourceRange.layerCount = 1;
            vkCreateImageView(device, &imgViewInfo, nullptr, &texo.imgv);
        }
    }

    void preTransitionImgLayout(VkImage img, VkImageLayout ol, VkImageLayout nl,
        VkPipelineStageFlags src, VkPipelineStageFlags dst) {
        VkImageMemoryBarrier imb {};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imb.oldLayout = ol;
        imb.newLayout = nl;
        imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.image = img;
        imb.subresourceRange.baseMipLevel = 0;
        imb.subresourceRange.levelCount = 1;
        imb.subresourceRange.baseArrayLayer = 0;
        imb.subresourceRange.layerCount = 1;

        if (nl == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        } else {
            imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        switch (ol) {
            case VK_IMAGE_LAYOUT_UNDEFINED:
                imb.srcAccessMask = 0;
                break;
            case VK_IMAGE_LAYOUT_PREINITIALIZED:
                imb.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
                break;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                imb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                break;
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                imb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;
            default:
                assert(0);
        }

        switch (nl) {
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                imb.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                break;
            case VK_IMAGE_LAYOUT_GENERAL:
                imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                break;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                break;
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                imb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                imb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                break;
            default:
                assert(0);
        }

        VkCommandBufferBeginInfo cbbi {};
        cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VkCommandBuffer & transitionCMD = cmdbuf[2];

        vkBeginCommandBuffer(transitionCMD, &cbbi);
        vkCmdPipelineBarrier(transitionCMD,
            src, dst,
            0,
            0, nullptr,
            0, nullptr,
            1, &imb);
        vkEndCommandBuffer(transitionCMD);

        VkSubmitInfo si {};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &transitionCMD;

        vkQueueSubmit(nongfxQ, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(nongfxQ);
    }

    void transitionImgLayout(VkCommandBuffer cmdbuf, VkImage img, VkImageLayout ol, VkImageLayout nl,
        VkPipelineStageFlags src, VkPipelineStageFlags dst) {
        VkImageMemoryBarrier imb {};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imb.oldLayout = ol;
        imb.newLayout = nl;
        imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.image = img;
        imb.subresourceRange.baseMipLevel = 0;
        imb.subresourceRange.levelCount = 1;
        imb.subresourceRange.baseArrayLayer = 0;
        imb.subresourceRange.layerCount = 1;

        if (nl == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        } else {
            imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        switch (ol) {
            case VK_IMAGE_LAYOUT_UNDEFINED:
                imb.srcAccessMask = 0;
                break;
            case VK_IMAGE_LAYOUT_PREINITIALIZED:
                imb.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
                break;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                imb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                break;
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                imb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;
            default:
                assert(0);
        }

        switch (nl) {
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                imb.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                break;
            case VK_IMAGE_LAYOUT_GENERAL:
                imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                break;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                break;
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                imb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                break;
            default:
                assert(0);
        }

        vkCmdPipelineBarrier(cmdbuf,
            src, dst,
            0,
            0, nullptr,
            0, nullptr,
            1, &imb);
    }

private:
    uint32_t findQueueFamilyIndex(VkQueueFlags desire) {
        for (uint32_t i = 0; i < queueFamily.size(); i++) {
            if (queueFamily[i].queueCount > 0 && (desire == (desire & queueFamily[i].queueFlags))) {
                return i;
            }
        }
        assert(0);
        return -1;
    }

    void initInstance() {
        vector<const char *> ie = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_XCB_SURFACE_EXTENSION_NAME,
            VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
        };

        VkInstanceCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.enabledExtensionCount = (uint32_t) ie.size();
        info.ppEnabledExtensionNames = ie.data();

        vkCreateInstance(&info, nullptr, &instance);
    }

    void initPhysicalDevice() {
        uint32_t cnt;
        vkEnumeratePhysicalDevices(instance, &cnt, nullptr);
        phydev.resize(cnt);
        vkEnumeratePhysicalDevices(instance, &cnt, phydev.data());

        vkGetPhysicalDeviceQueueFamilyProperties(phydev[0], &cnt, nullptr);
        queueFamily.resize(cnt);
        vkGetPhysicalDeviceQueueFamilyProperties(phydev[0], &cnt, queueFamily.data());

        vkGetPhysicalDeviceMemoryProperties(phydev[0], &pdmp);
        vkGetPhysicalDeviceProperties(phydev[0], &pdp);
    }

    void initDevice() {
        float priority[] = { 1.0 };
        gfxQueueIndex = findQueueFamilyIndex(VK_QUEUE_GRAPHICS_BIT);
        nongfxQueueIndex = findQueueFamilyIndex(VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT);
        vector<const char *> de = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
        };

        VkDeviceQueueCreateInfo queueInfo[2] {};
        queueInfo[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo[0].queueFamilyIndex = gfxQueueIndex;
        queueInfo[0].queueCount = 1;
        queueInfo[0].pQueuePriorities = priority;

        VkDeviceCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = queueInfo;
        info.enabledExtensionCount = (uint32_t) de.size();
        info.ppEnabledExtensionNames = de.data();

        if (gfxQueueIndex != nongfxQueueIndex) {

            queueInfo[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo[1].queueFamilyIndex = nongfxQueueIndex;
            queueInfo[1].queueCount = 1;
            queueInfo[1].pQueuePriorities = priority;

            info.queueCreateInfoCount = 2;
        }

        vkCreateDevice(phydev[0], &info, nullptr, &device);
        vkGetDeviceQueue(device, 0, 0, &gfxQ);
        vkGetDeviceQueue(device, 0, 0, &nongfxQ);
    }

    void initWSI() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfw = glfwCreateWindow(800, 800, __FILE__, nullptr, nullptr);

        glfwCreateWindowSurface(instance, glfw, nullptr, &surface);

        uint32_t fmtc;
        vkGetPhysicalDeviceSurfaceFormatsKHR(phydev[0], surface, &fmtc, nullptr);
        surfacefmtkhr.resize(fmtc);
        vkGetPhysicalDeviceSurfaceFormatsKHR(phydev[0], surface, &fmtc, surfacefmtkhr.data());
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phydev[0], surface, &surfacecapkhr);

        VkBool32 presentSupported = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(phydev[0], 0, surface, &presentSupported);
        assert(VK_TRUE == presentSupported);
    }

    void initSwapchain() {
        VkSwapchainCreateInfoKHR info {};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = surface;
        info.minImageCount = surfacecapkhr.minImageCount;
        info.imageFormat = surfacefmtkhr[0].format;
        info.imageColorSpace = surfacefmtkhr[0].colorSpace;
        info.imageExtent = surfacecapkhr.currentExtent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = surfacecapkhr.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        info.oldSwapchain = VK_NULL_HANDLE;

        vkCreateSwapchainKHR(device, &info, nullptr, &swapchain);

        uint32_t cnt;
        vkGetSwapchainImagesKHR(device, swapchain, &cnt, nullptr);
        swapchain_img.resize(cnt);
        swapchain_imgv.resize(cnt);
        vkGetSwapchainImagesKHR(device, swapchain, &cnt, swapchain_img.data());

        for (uint32_t i = 0; i < cnt; i++) {
            VkImageViewCreateInfo info {};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            info.image = swapchain_img[i];
            info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            info.format = surfacefmtkhr[0].format;
            info.subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                };

            vkCreateImageView(device, &info, nullptr, &swapchain_imgv[i]);
        }
    }

    void initCmdBuf() {
        VkCommandPoolCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        info.queueFamilyIndex = 0;

        vkCreateCommandPool(device, &info, nullptr, &cmdpool);

        /* 0 and 1 for gfx, 2 and 3 for nongfx */
        cmdbuf.resize(swapchain_img.size() + 2);
        VkCommandBufferAllocateInfo cmdBufInfo {};
        cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufInfo.commandPool = cmdpool;
        cmdBufInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufInfo.commandBufferCount = (uint32_t) cmdbuf.size();

        vkAllocateCommandBuffers(device, &cmdBufInfo, cmdbuf.data());
    }

    void initDepth() {
        VkImageCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_D32_SFLOAT;
        info.extent = {
                .width = surfacecapkhr.currentExtent.width,
                .height = surfacecapkhr.currentExtent.height,
                .depth = 1
            };
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        vkCreateImage(device, &info, nullptr, &depth_img);

        VkMemoryRequirements req {};
        vkGetImageMemoryRequirements(device, depth_img, &req);

        VkMemoryAllocateInfo ainfo {};
        ainfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ainfo.allocationSize = req.size;
        ainfo.memoryTypeIndex = resource_manager.findProperties(&pdmp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(device, &ainfo, nullptr, &depth_mem);

        vkBindImageMemory(device, depth_img, depth_mem, 0);

        VkImageViewCreateInfo vi {};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = depth_img;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_D32_SFLOAT;
        vi.subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            };

        vkCreateImageView(device, &vi, nullptr, &depth_imgv);

        preTransitionImgLayout(depth_img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
    }

    void initPSOTemplate() {
        /* fixed function viewport scissor state */
        fixfunc_templ.scissor = {
            .offset = { 0, 0 },
            .extent = surfacecapkhr.currentExtent };

        fixfunc_templ.vp = { 0, 0, float(surfacecapkhr.currentExtent.width),
            float(surfacecapkhr.currentExtent.height), 0, 1};

        fixfunc_templ.vpsInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .viewportCount = 1,
            .pViewports = &fixfunc_templ.vp,
            .scissorCount = 1,
            .pScissors = &fixfunc_templ.scissor
        };

        /* fixed function rasterization state */
        fixfunc_templ.rstInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0,
            .depthBiasClamp = 0.0,
            .depthBiasSlopeFactor = 0.0,
            .lineWidth = 1.0
        };

        /* fixed function MSAA state */
        fixfunc_templ.msaaInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 0.0,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE
        };

        /* fixed function DS state */
        fixfunc_templ.dsInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = {},
            .back = {},
            .minDepthBounds = 0.0,
            .maxDepthBounds = 0.0
        };

        /* fixed function blend state */
        fixfunc_templ.colorBldAttaState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        fixfunc_templ.colorBldAttaState.blendEnable = VK_FALSE;

        fixfunc_templ.bldInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_CLEAR,
            .attachmentCount = 1,
            .pAttachments = &fixfunc_templ.colorBldAttaState,
            .blendConstants = {},
        };

        /* fixed function dynamic state */
        fixfunc_templ.dynamicState[0] = VK_DYNAMIC_STATE_SCISSOR;
        fixfunc_templ.dynamicState[1] = VK_DYNAMIC_STATE_VIEWPORT;

        fixfunc_templ.dynamicInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .dynamicStateCount = 2,
            .pDynamicStates = fixfunc_templ.dynamicState,
        };
    }

public:
    GLFWwindow *glfw;
    VkDevice device;
    VkPhysicalDeviceMemoryProperties pdmp {};
    vector<VkSurfaceFormatKHR> surfacefmtkhr;
    VkSurfaceCapabilitiesKHR surfacecapkhr;
    VkSwapchainKHR swapchain;
    vector<VkImageView> swapchain_imgv;
    VkImageView depth_imgv;
    VkRenderPass renderpass;
    vector<VkFramebuffer> fb;
    ResouceMgnt resource_manager;
    PSOTemplate fixfunc_templ;
    vector<VkCommandBuffer> cmdbuf;
    VkQueue gfxQ; /* support GFX and presentation */
    VkQueue nongfxQ; /* support compute and transfer */
private:
    VkInstance instance;
    vector<VkPhysicalDevice> phydev;
    VkPhysicalDeviceProperties pdp {};
    vector<VkQueueFamilyProperties> queueFamily;
    uint32_t gfxQueueIndex;
    uint32_t nongfxQueueIndex;
    VkSurfaceKHR surface;
    vector<VkImage> swapchain_img;
    VkImage depth_img;
    VkDeviceMemory depth_mem;
    VkCommandPool cmdpool;
};

#endif

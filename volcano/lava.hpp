#ifndef _VOLCANO_HPP_
#define _VOLCANO_HPP_

#define VK_USE_PLATFORM_XCB_KHR
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
    VkDynamicState dynamicState[2] {};
    VkPipelineDynamicStateCreateInfo dynamicInfo {};
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
        vector<const char *> ie = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_XCB_SURFACE_EXTENSION_NAME,
            VK_EXT_DEBUG_REPORT_EXTENSION_NAME
        };

        VkInstanceCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pApplicationInfo = nullptr,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = (uint32_t) ie.size(),
            .ppEnabledExtensionNames = ie.data(),
        };

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
        float priority = 1.0;

        VkDeviceQueueCreateInfo queueInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = 0,
            .queueCount = 1,
            .pQueuePriorities = &priority
        };

        vector<const char *> de = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        VkDeviceCreateInfo deviceInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueInfo,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = (uint32_t) de.size(),
            .ppEnabledExtensionNames = de.data()
        };

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

        VkDebugReportCallbackCreateInfoEXT info = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT
                | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_ERROR_BIT_EXT
                | VK_DEBUG_REPORT_DEBUG_BIT_EXT,
            .pfnCallback = vcDbgReportCallback,
            .pUserData = this
        };
        dbgCreateDebugReportCallback(instance, &info, nullptr, &dbg_report_cb);
    }

    virtual void _initSwapchain() final {
        VkSwapchainCreateInfoKHR info = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .surface = surface,
            .minImageCount = surfacecapkhr.minImageCount,
            .imageFormat = surfacefmtkhr[0].format,
            .imageColorSpace = surfacefmtkhr[0].colorSpace,
            .imageExtent = surfacecapkhr.currentExtent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .preTransform = surfacecapkhr.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = VK_PRESENT_MODE_FIFO_KHR,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE
        };
        vkCreateSwapchainKHR(device, &info, nullptr, &swapchain);

        uint32_t cnt = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &cnt, nullptr);
        swapchain_img.resize(cnt);
        swapchain_imgv.resize(cnt);
        vkGetSwapchainImagesKHR(device, swapchain, &cnt, swapchain_img.data());

        for (uint32_t i = 0; i < cnt; i++) {
            VkImageViewCreateInfo imgvi = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .image = swapchain_img[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = surfacefmtkhr[0].format,
                .components = {
                    .r = VK_COMPONENT_SWIZZLE_R,
                    .g = VK_COMPONENT_SWIZZLE_G,
                    .b = VK_COMPONENT_SWIZZLE_B,
                    .a = VK_COMPONENT_SWIZZLE_A
                },
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };
            vkCreateImageView(device, &imgvi, nullptr, &swapchain_imgv[i]);
        }
    }

    virtual void _initRenderCmdBuf() final {
        VkCommandPoolCreateInfo cmdPoolInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = 0
        };
        vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &rendercmdpool);

        VkCommandBufferAllocateInfo cmdBufInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = rendercmdpool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = (uint32_t) swapchain_img.size()
        };
        rendercmdbuf.resize(swapchain_img.size());
        vkAllocateCommandBuffers(device, &cmdBufInfo, rendercmdbuf.data());
    }

    virtual void _initSyncObj() final {
        VkSemaphoreCreateInfo semaInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0
        };
        vkCreateSemaphore(device, &semaInfo, nullptr, &swapImgAcquire);
        vkCreateSemaphore(device, &semaInfo, nullptr, &renderImgFinished);
    }

    virtual void _bakeDepth() final {
        VkImageCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT,
            .extent = {
                .width = surfacecapkhr.currentExtent.width,
                .height = surfacecapkhr.currentExtent.height,
                .depth = 1
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        vkCreateImage(device, &info, nullptr, &depth_img);

        VkMemoryRequirements req = {};
        vkGetImageMemoryRequirements(device, depth_img, &req);

        VkMemoryAllocateInfo ainfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = req.size,
            .memoryTypeIndex = resource_manager.findProperties(&pdmp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        };
        vkAllocateMemory(device, &ainfo, nullptr, &depth_mem);

        vkBindImageMemory(device, depth_img, depth_mem, 0);

        VkImageViewCreateInfo dsImgViewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = depth_img,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT,
            .components = {},
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        vkCreateImageView(device, &dsImgViewInfo, nullptr, &depth_imgv);

        /* transition depth layout */
        VkCommandBuffer transitionCMD = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo transitionCMDAllocInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = rendercmdpool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        vkAllocateCommandBuffers(device, &transitionCMDAllocInfo, &transitionCMD);

        VkCommandBufferBeginInfo cbi = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
        };
        vkBeginCommandBuffer(transitionCMD, &cbi);

        VkImageMemoryBarrier imb = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = depth_img,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        vkCmdPipelineBarrier(transitionCMD, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imb);

        vkEndCommandBuffer(transitionCMD);
        VkSubmitInfo si = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .pWaitDstStageMask = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &transitionCMD,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = nullptr
        };
        vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);
        vkFreeCommandBuffers(device, rendercmdpool, 1, &transitionCMD);
    }

    virtual void _initRenderPass() final {
        VkAttachmentReference attRef[2] = {
            [0] = {
                .attachment = 0,
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            },
            [1] = {
                .attachment = 1,
                .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            }
        };

        VkAttachmentDescription attDesc[2] = {
            [0] = {
                .flags = 0,
                .format = surfacefmtkhr[0].format,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            },
            [1] = {
                .flags = 0,
                .format = VK_FORMAT_D32_SFLOAT,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            }
        };

        VkSubpassDescription spDesc = {
            .flags = 0,
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount = 0,
            .pInputAttachments = nullptr,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attRef[0],
            .pResolveAttachments = nullptr,
            .pDepthStencilAttachment = &attRef[1],
            .preserveAttachmentCount = 0,
            .pPreserveAttachments = nullptr,
        };

        VkSubpassDependency subpassDep[] = {
            [0] = {
                .srcSubpass = VK_SUBPASS_EXTERNAL,
                .dstSubpass = 0,
                .srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
            },
            [1] = {
                .srcSubpass = 0,
                .dstSubpass = VK_SUBPASS_EXTERNAL,
                .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
            },
        };

        VkRenderPassCreateInfo rpInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .attachmentCount = 2,
            .pAttachments = attDesc,
            .subpassCount = 1,
            .pSubpasses = &spDesc,
            .dependencyCount = 2,
            .pDependencies = subpassDep,
        };
        vkCreateRenderPass(device, &rpInfo, nullptr, &renderpass);
    }

    virtual void _initFramebuffer() final {
        fb.resize(swapchain_img.size());

        for (uint32_t i = 0; i < swapchain_img.size(); i++) {
            VkImageView att[2] = { swapchain_imgv[i], depth_imgv };

            VkFramebufferCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .renderPass = renderpass,
                .attachmentCount = 2,
                .pAttachments = att,
                .width = surfacecapkhr.currentExtent.width,
                .height = surfacecapkhr.currentExtent.height,
                .layers = 1,
            };
            vkCreateFramebuffer(device, &info, nullptr, &fb[i]);
        }
    }

    virtual void _bakePSOTemplate() final {
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
            if (hdrlength > 0 && ppchdrver == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
                vendorid == pdp.vendorID && deviceid == pdp.deviceID &&
                memcmp(uuid, pdp.pipelineCacheUUID, VK_UUID_SIZE) == 0) {
                cache_size = buffer.size();
                cache_datum = buffer.data();
            }
        }

        VkPipelineCacheCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .initialDataSize = cache_size,
            .pInitialData = cache_datum,
        };
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

            VkPipelineStageFlags ws = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

            VkSubmitInfo si = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                /* stages prior to output color stage can already process while output color stage must
                 * wait until _swpImgAcquire semaphore is signaled.
                 */
                .pNext = nullptr,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &swapImgAcquire,
                .pWaitDstStageMask = &ws,
                .commandBufferCount = 1,
                .pCommandBuffers = &rendercmdbuf[ImageIndex],
                /* signal finish of render process, status from unsignaled to signaled */
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &renderImgFinished,
            };
            vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);

            VkPresentInfoKHR pi = {
                .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                .pNext = nullptr,
                /* presentation engine can only start present image until the render executions onto the
                 * image have been finished (_renderImgFinished semaphore signaled)
                 */
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &renderImgFinished,
                .swapchainCount = 1,
                .pSwapchains = &swapchain,
                .pImageIndices = &ImageIndex,
                .pResults = nullptr
            };
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
    /* sync between presentation engine and start of command execution */
    VkSemaphore swapImgAcquire;
    /* sync between finish of command execution and present request to presentation engine */
    VkSemaphore renderImgFinished;
};

#endif

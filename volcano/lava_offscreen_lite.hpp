#ifndef _VOLCANO_LITE_HPP_
#define _VOLCANO_LITE_HPP_

#include <vulkan/vulkan.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <SOIL/SOIL.h>

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
        for (const auto iter : texDustbin) {
            vkDestroyImageView(device, iter.imgv, nullptr);
            vkFreeMemory(device, iter.mem, nullptr);
            vkDestroyImage(device, iter.img, nullptr);
        }

        vkDestroyFramebuffer(device, fb, nullptr);
        vkDestroyRenderPass(device, renderpass, nullptr);

        vkFreeCommandBuffers(device, cmdpool, cmdbuf.size(), cmdbuf.data());
        vkDestroyCommandPool(device, cmdpool, nullptr);

        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
    }

    Volcano() = delete;
    Volcano(uint32_t w, uint32_t h) {
        initInstance();
        initPhysicalDevice();
        initDevice();
        initCmdBuf();
        initPSOTemplate(w, h);
        initDepth(w, h);
        initRenderTarget(w, h);
        initStageTexture(w, h);
        initRenderpass();
        initFramebuffer(w, h);
    }

    struct TexObj {
        VkImage img {};
        VkDeviceMemory mem {};
        VkImageView imgv {};
    };

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
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                imb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
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

        VkCommandBuffer & transitionCMD = cmdbuf[1];

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
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                imb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
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

        vkCmdPipelineBarrier(cmdbuf,
            src, dst,
            0,
            0, nullptr,
            0, nullptr,
            1, &imb);
    }

    void diskRTImage(const string & filename, uint32_t w, uint32_t h) {
        VkCommandBufferBeginInfo cbbi {};
        cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbbi.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        VkCommandBuffer & transitionCMD = cmdbuf[1];

        vkBeginCommandBuffer(transitionCMD, &cbbi);

        transitionImgLayout(transitionCMD, renderTargetTexObj.img,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkImageCopy region {};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.mipLevel = 0;
        region.srcSubresource.baseArrayLayer = 0;
        region.srcSubresource.layerCount = 1;
        region.srcOffset = { 0, 0, 0 };
        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.mipLevel = 0;
        region.dstSubresource.baseArrayLayer = 0;
        region.dstSubresource.layerCount = 1;
        region.dstOffset = { 0, 0, 0 };
        region.extent = { w, h, 1 };

        vkCmdCopyImage(transitionCMD,
            renderTargetTexObj.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            stagingLinearTexObj.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &region);
        vkEndCommandBuffer(transitionCMD);

        VkSubmitInfo si {};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &transitionCMD;

        vkQueueSubmit(nongfxQ, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(nongfxQ);

        VkImageSubresource iss {};
        iss.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        iss.mipLevel = 0;
        iss.arrayLayer = 0;
        VkSubresourceLayout sl {};
        vkGetImageSubresourceLayout(device, stagingLinearTexObj.img, &iss, &sl);

        uint8_t *pDST = nullptr;
        uint8_t *pSRC = new uint8_t [sl.size];

        vkMapMemory(device, stagingLinearTexObj.mem, 0, sl.size, 0, (void **)&pDST);
        for (int i = 0; i < h; i++) {
            memcpy(pSRC + i*w*4, pDST + i*sl.rowPitch, w*4);
        }
        vkUnmapMemory(device, stagingLinearTexObj.mem);
        SOIL_save_image(filename.c_str(), SOIL_SAVE_TYPE_TGA, w, h, 4, pSRC);
        delete [] pSRC;
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
            VK_EXT_DEBUG_REPORT_EXTENSION_NAME
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

        VkDeviceQueueCreateInfo queueInfo[2] {};
        queueInfo[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo[0].queueFamilyIndex = gfxQueueIndex;
        queueInfo[0].queueCount = 1;
        queueInfo[0].pQueuePriorities = priority;

        VkDeviceCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = queueInfo;

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

    void initCmdBuf() {
        VkCommandPoolCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        info.queueFamilyIndex = 0;

        vkCreateCommandPool(device, &info, nullptr, &cmdpool);

        /* 0 for gfx, 1 for nongfx */
        cmdbuf.resize(2);
        VkCommandBufferAllocateInfo cmdBufInfo {};
        cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufInfo.commandPool = cmdpool;
        cmdBufInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufInfo.commandBufferCount = (uint32_t) cmdbuf.size();

        vkAllocateCommandBuffers(device, &cmdBufInfo, cmdbuf.data());
    }

    void initPSOTemplate(uint32_t w, uint32_t h) {
        /* fixed function viewport scissor state */
        fixfunc_templ.scissor = {
            .offset = { 0, 0 },
            .extent = { w, h },
        };

        fixfunc_templ.vp = { 0, 0, float(w), float(h), 0, 1 };

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

    void initRenderpass() {
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
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            },
            [1] = {
                .flags = 0,
                .format = VK_FORMAT_D32_SFLOAT,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
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

        VkSubpassDependency subpassDep = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = 0,
        };

        VkRenderPassCreateInfo rpInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .attachmentCount = 2,
            .pAttachments = attDesc,
            .subpassCount = 1,
            .pSubpasses = &spDesc,
            .dependencyCount = 1,
            .pDependencies = &subpassDep,
        };
        vkCreateRenderPass(device, &rpInfo, nullptr, &renderpass);
    }

    void initFramebuffer(uint32_t w, uint32_t h) {
        VkImageView att[2] = { renderTargetTexObj.imgv, depthTexObj.imgv };

        VkFramebufferCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .renderPass = renderpass,
            .attachmentCount = 2,
            .pAttachments = att,
            .width = w,
            .height = h,
            .layers = 1,
        };
        vkCreateFramebuffer(device, &info, nullptr, &fb);
    }

    void initDepth(uint32_t w, uint32_t h) {
        VkImageCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_D32_SFLOAT;
        info.extent = {
                .width = w,
                .height = h,
                .depth = 1
            };
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        vkCreateImage(device, &info, nullptr, &depthTexObj.img);

        VkMemoryRequirements req {};
        vkGetImageMemoryRequirements(device, depthTexObj.img, &req);

        VkMemoryAllocateInfo ainfo {};
        ainfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ainfo.allocationSize = req.size;
        ainfo.memoryTypeIndex = resource_manager.findProperties(&pdmp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(device, &ainfo, nullptr, &depthTexObj.mem);

        vkBindImageMemory(device, depthTexObj.img, depthTexObj.mem, 0);

        VkImageViewCreateInfo vi {};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = depthTexObj.img;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_D32_SFLOAT;
        vi.subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            };

        vkCreateImageView(device, &vi, nullptr, &depthTexObj.imgv);

        preTransitionImgLayout(depthTexObj.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

        texDustbin.push_back(depthTexObj);
    }

    void initRenderTarget(uint32_t w, uint32_t h) {
        VkImageCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.extent = {
                .width = w,
                .height = h,
                .depth = 1
            };
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        vkCreateImage(device, &info, nullptr, &renderTargetTexObj.img);

        VkMemoryRequirements req {};
        vkGetImageMemoryRequirements(device, renderTargetTexObj.img, &req);

        VkMemoryAllocateInfo ainfo {};
        ainfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ainfo.allocationSize = req.size;
        ainfo.memoryTypeIndex = resource_manager.findProperties(&pdmp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(device, &ainfo, nullptr, &renderTargetTexObj.mem);

        vkBindImageMemory(device, renderTargetTexObj.img, renderTargetTexObj.mem, 0);

        VkImageViewCreateInfo vi {};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = renderTargetTexObj.img;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            };

        vkCreateImageView(device, &vi, nullptr, &renderTargetTexObj.imgv);

        texDustbin.push_back(renderTargetTexObj);
    }

    void initStageTexture(uint32_t w, uint32_t h) {
        VkImageCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.extent = {
                .width = w,
                .height = h,
                .depth = 1
            };
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_LINEAR;
        info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        vkCreateImage(device, &info, nullptr, &stagingLinearTexObj.img);

        VkMemoryRequirements req {};
        vkGetImageMemoryRequirements(device, stagingLinearTexObj.img, &req);

        VkMemoryAllocateInfo ainfo {};
        ainfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ainfo.allocationSize = req.size;
        ainfo.memoryTypeIndex = resource_manager.findProperties(&pdmp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(device, &ainfo, nullptr, &stagingLinearTexObj.mem);

        vkBindImageMemory(device, stagingLinearTexObj.img, stagingLinearTexObj.mem, 0);

        preTransitionImgLayout(stagingLinearTexObj.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        texDustbin.push_back(stagingLinearTexObj);
    }

public:
    VkDevice device;
    VkPhysicalDeviceMemoryProperties pdmp {};
    TexObj depthTexObj;
    TexObj renderTargetTexObj; /* attach to framebuffer for render result's content (tile) */
    TexObj stagingLinearTexObj; /* convert render result's content to disk storage (linear) */
    vector<TexObj> texDustbin; /* collection of texture objects */
    ResouceMgnt resource_manager;
    PSOTemplate fixfunc_templ;
    vector<VkCommandBuffer> cmdbuf;
    VkQueue gfxQ; /* support GFX and presentation */
    VkQueue nongfxQ; /* support compute and transfer */
    VkRenderPass renderpass;
    VkFramebuffer fb;
private:
    VkInstance instance;
    vector<VkPhysicalDevice> phydev;
    VkPhysicalDeviceProperties pdp {};
    vector<VkQueueFamilyProperties> queueFamily;
    uint32_t gfxQueueIndex;
    uint32_t nongfxQueueIndex;
    VkCommandPool cmdpool;
};

#endif

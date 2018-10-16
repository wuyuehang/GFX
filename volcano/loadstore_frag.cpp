#include "lava.hpp"
#include <SOIL/SOIL.h>

class App : public Volcano {
public:
    ~App() {
        vkQueueWaitIdle(queue);

        vkFreeDescriptorSets(device, descPool, 1, &descSet);
        vkDestroyDescriptorPool(device, descPool, nullptr);
        vkDestroyDescriptorSetLayout(device, descSetLayout, nullptr);

        vkDestroyPipelineLayout(device, layout, nullptr);
        vkDestroyPipeline(device, gfxPipeline, nullptr);

        resource_manager.freeBuf(device);
        vkDestroyImageView(device, tx2d_imgv, nullptr);
        vkFreeMemory(device, tx2d_mem, nullptr);
        vkDestroyImage(device, tx2d_img, nullptr);;
    }

    App() {
        InitBuffers();
#if 1
        BakeLinearTexture2D();
#else
        BakeTexture2D();
#endif
        InitGFXPipeline();
        InitDescriptor();
        BakeCommand();
    }

    void BakeCommand() {
        VkCommandBufferBeginInfo cbi = {};
        cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbi.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        for (uint8_t i = 0; i < rendercmdbuf.size(); i++) {
            vkBeginCommandBuffer(rendercmdbuf[i], &cbi);

            VkRenderPassBeginInfo rpBeginInfo = {};
            rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpBeginInfo.renderPass = renderpass;
            rpBeginInfo.framebuffer = fb[i];
            rpBeginInfo.renderArea.offset = {0, 0};
            rpBeginInfo.renderArea.extent = surfacecapkhr.currentExtent;

            VkClearValue cvs[2] = {};
            cvs[0].color = {0.01, 0.02, 0.0, 1.0};
            cvs[1].depthStencil = { 1.0, 0 };
            rpBeginInfo.clearValueCount = 2;
            rpBeginInfo.pClearValues = cvs;

            vkCmdBeginRenderPass(rendercmdbuf[i], &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(rendercmdbuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS, gfxPipeline);
            vkCmdBindDescriptorSets(rendercmdbuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                layout, 0, 1, &descSet, 0, nullptr);
            VkDeviceSize offset = {};
            VkBuffer _vertexBuf = resource_manager.queryBuf("vertexbuffer");
            vkCmdBindVertexBuffers(rendercmdbuf[i], 0, 1, &_vertexBuf, &offset);
            VkRect2D scissor = { 10, 10, 780, 780 };
            vkCmdSetScissor(rendercmdbuf[i], 0, 1, &scissor);
            VkViewport vp = { 0.0, 0.0, 800, 800, 0.0, 1.0 };
            vkCmdSetViewport(rendercmdbuf[i], 0, 1, &vp);
            vkCmdDraw(rendercmdbuf[i], 4, 1, 0, 0);
            vkCmdEndRenderPass(rendercmdbuf[i]);

            vkEndCommandBuffer(rendercmdbuf[i]);
        }
    }

    void BakeTexture2D() {
        int width, height;
        uint8_t *img = SOIL_load_image("ReneDescartes.jpeg", &width, &height, 0, SOIL_LOAD_RGBA);

        VkBuffer srcBuf = resource_manager.allocBuf(device, pdmp, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            width*height*4, img, "stagingbuffer", VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        SOIL_free_image_data(img);

        VkImageCreateInfo imgInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = {
                .width = (uint32_t) width,
                .height = (uint32_t) height,
                .depth = 1,
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        vkCreateImage(device, &imgInfo, nullptr, &tx2d_img);

        VkMemoryRequirements req = {};
        vkGetImageMemoryRequirements(device, tx2d_img, &req);

        VkMemoryAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = req.size,
            .memoryTypeIndex = resource_manager.findProperties(&pdmp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };
        vkAllocateMemory(device, &allocInfo, nullptr, &tx2d_mem);

        vkBindImageMemory(device, tx2d_img, tx2d_mem, 0);

        VkImageViewCreateInfo imgViewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = tx2d_img,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_R,
                .g = VK_COMPONENT_SWIZZLE_G,
                .b = VK_COMPONENT_SWIZZLE_B,
                .a = VK_COMPONENT_SWIZZLE_A,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        };
        vkCreateImageView(device, &imgViewInfo, nullptr, &tx2d_imgv);

        /* transition texture layout */
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
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = tx2d_img,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        vkCmdPipelineBarrier(transitionCMD, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imb);

        VkBufferImageCopy rg = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = VkOffset3D{ 0, 0, 0 },
            .imageExtent = VkExtent3D{ uint32_t(width), uint32_t(height), 1 },
        };
        vkCmdCopyBufferToImage(transitionCMD, srcBuf, tx2d_img,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);

        imb = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = tx2d_img,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        vkCmdPipelineBarrier(transitionCMD, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imb);

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

    void BakeLinearTexture2D() {
        int width, height;
        uint8_t *img = SOIL_load_image("ReneDescartes.jpeg", &width, &height, 0, SOIL_LOAD_RGBA);

        VkImageCreateInfo imgInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = {
                .width = (uint32_t) width,
                .height = (uint32_t) height,
                .depth = 1
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_LINEAR,
            .usage = VK_IMAGE_USAGE_STORAGE_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
        };
        vkCreateImage(device, &imgInfo, nullptr, &tx2d_img);

        VkMemoryRequirements req = {};
        vkGetImageMemoryRequirements(device, tx2d_img, &req);

        VkMemoryAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = req.size,
            .memoryTypeIndex = resource_manager.findProperties(&pdmp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };
        vkAllocateMemory(device, &allocInfo, nullptr, &tx2d_mem);

        vkBindImageMemory(device, tx2d_img, tx2d_mem, 0);

        VkImageViewCreateInfo imgViewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = tx2d_img,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_R,
                .g = VK_COMPONENT_SWIZZLE_G,
                .b = VK_COMPONENT_SWIZZLE_B,
                .a = VK_COMPONENT_SWIZZLE_A,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        };
        vkCreateImageView(device, &imgViewInfo, nullptr, &tx2d_imgv);

        VkImageSubresource subresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .arrayLayer = 0,
        };

        VkSubresourceLayout subresource_layout = {};
        vkGetImageSubresourceLayout(device, tx2d_img, &subresource, &subresource_layout);

        uint8_t *pDST = nullptr;

        vkMapMemory(device, tx2d_mem, 0, req.size, 0, (void **)&pDST);
        for (int i = 0; i < height; i++) {
            memcpy(pDST, (img + width * i * 4), width * 4);
            pDST = pDST + subresource_layout.rowPitch;
        }
        vkUnmapMemory(device, tx2d_mem);

        SOIL_free_image_data(img);

        /* transition texture layout */
        VkCommandBuffer transitionCMD = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo transitionCMDAllocInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = rendercmdpool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
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
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = tx2d_img,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        };
        vkCmdPipelineBarrier(transitionCMD, VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imb);

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

    void InitBuffers() {
        float position[] = {
            -1.0, -1.0, -1.0, 1.0, 1.0, -1.0, 1.0, 1.0,
        };

        resource_manager.allocBuf(device, pdmp, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                sizeof(position), position, "vertexbuffer", VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    void InitGFXPipeline() {
        /* graphics pipeline -- shader */
        VkShaderModule vertShaderModule = VK_NULL_HANDLE;
        auto vert = loadSPIRV("single_attribute_nonmvp.vert.spv");
        VkShaderModuleCreateInfo vertShaderModuleInfo = {};
        vertShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vertShaderModuleInfo.codeSize = vert.size();
        vertShaderModuleInfo.pCode = (const uint32_t *)vert.data();
        vkCreateShaderModule(device, &vertShaderModuleInfo, nullptr, &vertShaderModule);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkShaderModule fragShaderModule = VK_NULL_HANDLE;
        auto frag = loadSPIRV("loadstore_frag.frag.spv");
        VkShaderModuleCreateInfo fragShaderModuleInfo = {};
        fragShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        fragShaderModuleInfo.codeSize = frag.size();
        fragShaderModuleInfo.pCode = (const uint32_t *)frag.data();
        vkCreateShaderModule(device, &fragShaderModuleInfo, nullptr, &fragShaderModule);

        VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStageInfos[2] = { vertShaderStageInfo, fragShaderStageInfo };

        /* graphics pipeline -- state */
        VkVertexInputBindingDescription vbd = {
            .binding = 0,
            .stride = 2*sizeof(float),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };

        VkVertexInputAttributeDescription ad = {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = 0,
        };

        VkPipelineVertexInputStateCreateInfo vertInputInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &vbd,
            .vertexAttributeDescriptionCount = 1,
            .pVertexAttributeDescriptions = &ad,
        };

        VkPipelineInputAssemblyStateCreateInfo iaInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
            .primitiveRestartEnable = VK_FALSE,
        };

        VkDescriptorSetLayoutBinding binding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        };

        VkDescriptorSetLayoutCreateInfo dsLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = 1,
            .pBindings = &binding
        };
        vkCreateDescriptorSetLayout(device, &dsLayoutInfo, nullptr, &descSetLayout);

        VkPipelineLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = 1,
            .pSetLayouts = &descSetLayout,
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = nullptr
        };
        vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout);

        VkGraphicsPipelineCreateInfo gfxPipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stageCount = 2,
            .pStages = shaderStageInfos,
            .pVertexInputState = &vertInputInfo,
            .pInputAssemblyState = &iaInfo,
            .pTessellationState = nullptr,
            .pViewportState = &fixfunc_templ.vpsInfo,
            .pRasterizationState = &fixfunc_templ.rstInfo,
            .pMultisampleState = &fixfunc_templ.msaaInfo,
            .pDepthStencilState = &fixfunc_templ.dsInfo,
            .pColorBlendState = &fixfunc_templ.bldInfo,
            .pDynamicState = &fixfunc_templ.dynamicInfo,
            .layout = layout,
            .renderPass = renderpass,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0
        };
        vkCreateGraphicsPipelines(device, nullptr, 1, &gfxPipelineInfo, nullptr, &gfxPipeline);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
    }

    void InitDescriptor() {
        VkDescriptorPoolSize poolSize = {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
        };

        VkDescriptorPoolCreateInfo dsPoolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize,
        };
        vkCreateDescriptorPool(device, &dsPoolInfo, nullptr, &descPool);

        VkDescriptorSetAllocateInfo dsAllocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = descPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &descSetLayout,
        };
        vkAllocateDescriptorSets(device, &dsAllocInfo, &descSet);

        /* Update DescriptorSets */
        VkDescriptorImageInfo descImgInfo = {
            .sampler = nullptr,
            .imageView = tx2d_imgv,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };

        VkWriteDescriptorSet wds = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = descSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &descImgInfo,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
        };
        vkUpdateDescriptorSets(device, 1, &wds, 0, nullptr);
    }

public:
    VkImage tx2d_img;
    VkDeviceMemory tx2d_mem;
    VkImageView tx2d_imgv;
    VkPipelineLayout layout;
    VkPipeline gfxPipeline;
    VkDescriptorSetLayout descSetLayout;
    VkDescriptorPool descPool;
    VkDescriptorSet descSet;
};

int main(int argc, char const *argv[])
{
    App app;
    app.Run();
    return 0;
}

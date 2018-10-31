#include "lava_lite.hpp"
#include <SOIL/SOIL.h>

class App : public Volcano {
public:
    ~App() {
        vkQueueWaitIdle(gfxQ);

        vkDestroyDescriptorSetLayout(device, sp0sp1_descset_layout, nullptr);
        vkDestroyPipelineLayout(device, sp0sp1_pipeline_layout, nullptr);
        for (uint32_t i = 0; i < SPMAX; i++) {
            vkFreeDescriptorSets(device, descpool, 1, &descset[i]);
            vkDestroyPipeline(device, pipeline[i], nullptr);
        }
        vkDestroyDescriptorPool(device, descpool, nullptr);

        vkDestroySemaphore(device, presentImgFinished, nullptr);
        vkDestroySemaphore(device, renderImgFinished, nullptr);
        for (const auto iter : fb) {
            vkDestroyFramebuffer(device, iter, nullptr);
        }
        vkDestroyRenderPass(device, renderpass, nullptr);
        vkDestroySampler(device, smp, nullptr);
        for (const auto iter : RT) {
            vkDestroyImageView(device, iter.imgv, nullptr);
            vkFreeMemory(device, iter.memory, nullptr);
            vkDestroyImage(device, iter.img, nullptr);
        }
        vkDestroyImageView(device, fogsmoke.imgv, nullptr);
        vkFreeMemory(device, fogsmoke.memory, nullptr);
        vkDestroyImage(device, fogsmoke.img, nullptr);
        resource_manager.freeBuf(device);
    }

    App() {
        initBuffer();
        initTexture();
        initSubpassRenderTarget();
        initSampler();
        initRenderpass();
        initFramebuffer();
        initSync();
        initDescriptorSetLayout();
        initGFXPipeline();
        initDescriptor();
        initGFXCommand();
    }

    void run() {
        glfwShowWindow(glfw);
        uint32_t ImageIndex = 0;
        while (!glfwWindowShouldClose(glfw)) {
            glfwPollEvents();

            vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, presentImgFinished, VK_NULL_HANDLE, &ImageIndex);

            {
                VkPipelineStageFlags ws[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

                VkSubmitInfo gfxSubmitInfo = {
                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    .pNext = nullptr,
                    .waitSemaphoreCount = 1,
                    .pWaitSemaphores = &presentImgFinished,
                    .pWaitDstStageMask = ws,
                    .commandBufferCount = 1,
                    .pCommandBuffers = &cmdbuf[ImageIndex],
                    .signalSemaphoreCount = 1,
                    .pSignalSemaphores = &renderImgFinished,
                };
                vkQueueSubmit(gfxQ, 1, &gfxSubmitInfo, VK_NULL_HANDLE);
            }

            VkPresentInfoKHR pi = {
                .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                .pNext = nullptr,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &renderImgFinished,
                .swapchainCount = 1,
                .pSwapchains = &swapchain,
                .pImageIndices = &ImageIndex,
                .pResults = nullptr
            };
            vkQueuePresentKHR(gfxQ, &pi);
        }
    }

    void initBuffer() {
        float quad_mesh[] = {
            -1.0, 1.0, 0.0, 1.0,
            -1.0, -1.0, 0.0, 0.0,
            1.0, 1.0, 1.0, 1.0,
            1.0, -1.0, 1.0, 0.0
        };

        resource_manager.allocBuf(device, pdmp, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            sizeof(quad_mesh), quad_mesh, "vertexbuf", VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vibd[0].binding = 0;
        vibd[0].stride = 4*sizeof(float);
        vibd[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        viad[0].location = 0;
        viad[0].binding = 0;
        viad[0].format = VK_FORMAT_R32G32_SFLOAT;
        viad[0].offset = 0;
        viad[1].location = 1;
        viad[1].binding = 0;
        viad[1].format = VK_FORMAT_R32G32_SFLOAT;
        viad[1].offset = 2*sizeof(float);
    }

    void initTexture() {
        int width, height;
        uint8_t *img = SOIL_load_image("green-fog-smoke.jpg", &width, &height, 0, SOIL_LOAD_RGBA);

        bakeImage(fogsmoke, VK_FORMAT_R8G8B8A8_UNORM, width, height, VK_IMAGE_TILING_LINEAR,
            VK_IMAGE_USAGE_SAMPLED_BIT, img);

        SOIL_free_image_data(img);

        preTransitionImgLayout(fogsmoke.img, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }

    void initSubpassRenderTarget() {
        bakeImage(RT[SP0], VK_FORMAT_R8G8B8A8_UNORM,
            surfacecapkhr.currentExtent.width, surfacecapkhr.currentExtent.height,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, nullptr);

        preTransitionImgLayout(RT[SP0].img,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    }

    void initSampler() {
        VkSamplerCreateInfo smpInfo {};
        smpInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        smpInfo.minFilter = VK_FILTER_LINEAR;
        smpInfo.magFilter = VK_FILTER_LINEAR;
        smpInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        smpInfo.mipLodBias = 0.0;
        smpInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        smpInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        smpInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        smpInfo.anisotropyEnable = VK_FALSE;
        smpInfo.compareEnable = VK_FALSE;
        smpInfo.minLod = 0.0;
        smpInfo.maxLod = 0.0;
        smpInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        smpInfo.unnormalizedCoordinates = VK_FALSE;
        vkCreateSampler(device, &smpInfo, nullptr, &smp);
    }

    void initRenderpass() {
        array<VkAttachmentReference, 3> attRef {};
        attRef[0] = {
            .attachment = 0,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        attRef[1] = {
            .attachment = 1,
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
        attRef[2] = {
            .attachment = 2,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };

        array<VkAttachmentDescription, 3> attDesc {};
        /* swapchain image */
        attDesc[0] = {
            .flags = 0,
            .format = surfacefmtkhr[0].format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        };
        /* depth image */
        attDesc[1] = {
            .flags = 0,
            .format = VK_FORMAT_D32_SFLOAT,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
        /* subpass 0 render target */
        attDesc[2] = {
            .flags = 0,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };

        array<VkSubpassDescription, 2> spDesc {};
        /* subpass 0 : fogsmoke process */
        spDesc[0] = {
            .flags = 0,
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount = 0,
            .pInputAttachments = nullptr,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attRef[2],
            .pResolveAttachments = nullptr,
            .pDepthStencilAttachment = nullptr,
            .preserveAttachmentCount = 0,
            .pPreserveAttachments = nullptr,
        };
        /* subpass 1 : composite */
        spDesc[1] = {
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

        array<VkSubpassDependency, 3> subpassDep {};
        /* outside world to subpass 0 */
        subpassDep[0] = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        };
        /* subpass 0 to subpass 1 */
        subpassDep[1] = {
            .srcSubpass = 0,
            .dstSubpass = 1,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        };
        /* subpass 1 to outside world */
        subpassDep[2] = {
            .srcSubpass = 1,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        };

        VkRenderPassCreateInfo rpInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .attachmentCount = attDesc.size(),
            .pAttachments = attDesc.data(),
            .subpassCount = spDesc.size(),
            .pSubpasses = spDesc.data(),
            .dependencyCount = subpassDep.size(),
            .pDependencies = subpassDep.data(),
        };
        vkCreateRenderPass(device, &rpInfo, nullptr, &renderpass);
    }

    void initFramebuffer() {
        fb.resize(swapchain_imgv.size());

        for (uint32_t i = 0; i < swapchain_imgv.size(); i++) {
            array<VkImageView, 3> att {};
            att[0] = swapchain_imgv[i];
            att[1] = depth_imgv;
            att[2] = RT[SP0].imgv;

            VkFramebufferCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .renderPass = renderpass,
                .attachmentCount = att.size(),
                .pAttachments = att.data(),
                .width = surfacecapkhr.currentExtent.width,
                .height = surfacecapkhr.currentExtent.height,
                .layers = 1,
            };
            vkCreateFramebuffer(device, &info, nullptr, &fb[i]);
        }
    }

    void initSync() {
        VkSemaphoreCreateInfo semaInfo {};
        semaInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        vkCreateSemaphore(device, &semaInfo, nullptr, &presentImgFinished);
        vkCreateSemaphore(device, &semaInfo, nullptr, &renderImgFinished);
    }

    void initDescriptorSetLayout() {
        /* subpass 0 and subpass 1 */
        array<VkDescriptorSetLayoutBinding, 1> bindings {};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[0].pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo dsLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = bindings.size(),
            .pBindings = bindings.data(),
        };
        vkCreateDescriptorSetLayout(device, &dsLayoutInfo, nullptr, &sp0sp1_descset_layout);

        VkPipelineLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = 1,
            .pSetLayouts = &sp0sp1_descset_layout,
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = nullptr
        };
        vkCreatePipelineLayout(device, &layoutInfo, nullptr, &sp0sp1_pipeline_layout);
    }

    void initGFXPipeline() {
        VkShaderModule vertShaderModule = initShaderModule("quad.vert.spv");
        VkShaderModule fragShaderModule = initShaderModule("quad.frag.spv");

        array<VkPipelineShaderStageCreateInfo, 2> shaderStageInfo {};
        shaderStageInfo[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStageInfo[0].module = vertShaderModule;
        shaderStageInfo[0].pName = "main";

        shaderStageInfo[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStageInfo[1].module = fragShaderModule;
        shaderStageInfo[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertInputInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = (uint32_t) vibd.size(),
            .pVertexBindingDescriptions = vibd.data(),
            .vertexAttributeDescriptionCount = (uint32_t) viad.size(),
            .pVertexAttributeDescriptions = viad.data(),
        };

        VkPipelineInputAssemblyStateCreateInfo iaInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
            .primitiveRestartEnable = VK_FALSE,
        };

        VkGraphicsPipelineCreateInfo gfxPipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stageCount = shaderStageInfo.size(),
            .pStages = shaderStageInfo.data(),
            .pVertexInputState = &vertInputInfo,
            .pInputAssemblyState = &iaInfo,
            .pTessellationState = nullptr,
            .pViewportState = &fixfunc_templ.vpsInfo,
            .pRasterizationState = &fixfunc_templ.rstInfo,
            .pMultisampleState = &fixfunc_templ.msaaInfo,
            .pDepthStencilState = &fixfunc_templ.dsInfo,
            .pColorBlendState = &fixfunc_templ.bldInfo,
            .pDynamicState = &fixfunc_templ.dynamicInfo,
            .layout = sp0sp1_pipeline_layout,
            .renderPass = renderpass,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0
        };
        vkCreateGraphicsPipelines(device, nullptr, 1, &gfxPipelineInfo, nullptr, &pipeline[SP0]);

        gfxPipelineInfo.subpass = 1;
        vkCreateGraphicsPipelines(device, nullptr, 1, &gfxPipelineInfo, nullptr, &pipeline[SP1]);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
    }

    void initDescriptor() {
        array<VkDescriptorPoolSize, 1> poolSize {};
        poolSize[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize[0].descriptorCount = 2; // sp0 and sp1 individually consume

        VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 2, // sp0 and sp1 individually consume
            .poolSizeCount = poolSize.size(),
            .pPoolSizes = poolSize.data(),
        };
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &descpool);

        {
            VkDescriptorSetAllocateInfo ainfo = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .pNext = nullptr,
                .descriptorPool = descpool,
                .descriptorSetCount = 1,
                .pSetLayouts = &sp0sp1_descset_layout,
            };
            vkAllocateDescriptorSets(device, &ainfo, &descset[SP0]);

            array<VkDescriptorImageInfo, 1> descImgInfo {};
            descImgInfo[0].sampler = smp;
            descImgInfo[0].imageView = fogsmoke.imgv;
            descImgInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            array<VkWriteDescriptorSet, 1> wds {};
            wds[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wds[0].pNext = nullptr;
            wds[0].dstSet = descset[SP0];
            wds[0].dstBinding = 0;
            wds[0].dstArrayElement = 0;
            wds[0].descriptorCount = 1;
            wds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            wds[0].pImageInfo = &descImgInfo[0];
            wds[0].pBufferInfo = nullptr;
            wds[0].pTexelBufferView = nullptr;
            vkUpdateDescriptorSets(device, wds.size(), wds.data(), 0, nullptr);
        }
        {
            VkDescriptorSetAllocateInfo ainfo = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .pNext = nullptr,
                .descriptorPool = descpool,
                .descriptorSetCount = 1,
                .pSetLayouts = &sp0sp1_descset_layout,
            };
            vkAllocateDescriptorSets(device, &ainfo, &descset[SP1]);

            array<VkDescriptorImageInfo, 1> descImgInfo {};
            descImgInfo[0].sampler = smp;
            descImgInfo[0].imageView = RT[SP0].imgv;
            descImgInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            array<VkWriteDescriptorSet, 1> wds {};
            wds[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wds[0].pNext = nullptr;
            wds[0].dstSet = descset[SP1];
            wds[0].dstBinding = 0;
            wds[0].dstArrayElement = 0;
            wds[0].descriptorCount = 1;
            wds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            wds[0].pImageInfo = &descImgInfo[0];
            wds[0].pBufferInfo = nullptr;
            wds[0].pTexelBufferView = nullptr;
            vkUpdateDescriptorSets(device, wds.size(), wds.data(), 0, nullptr);
        }
    }

    void initGFXCommand() {
        VkCommandBufferBeginInfo cbi = {};
        cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbi.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        for (uint8_t i = 0; i < swapchain_imgv.size(); i++) {
            vkBeginCommandBuffer(cmdbuf[i], &cbi);

            VkRenderPassBeginInfo rpBeginInfo = {};
            rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpBeginInfo.renderPass = renderpass;
            rpBeginInfo.framebuffer = fb[i];
            rpBeginInfo.renderArea.offset = {0, 0};
            rpBeginInfo.renderArea.extent = surfacecapkhr.currentExtent;

            array<VkClearValue, 3> cvs = {};
            cvs[0].color = { 0.0, 0.0, 0.0, 1.0 };
            cvs[1].depthStencil = { 1.0, 0 };
            cvs[2].color = { 0.0, 0.0, 0.0, 1.0 };
            rpBeginInfo.clearValueCount = cvs.size();
            rpBeginInfo.pClearValues = cvs.data();

            vkCmdBeginRenderPass(cmdbuf[i], &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            /* subpass 0 paints green fog */
            vkCmdBindPipeline(cmdbuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline[SP0]);
            vkCmdBindDescriptorSets(cmdbuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                sp0sp1_pipeline_layout, 0, 1, &descset[SP0], 0, nullptr);
            VkDeviceSize offset = {};
            VkBuffer _vertexBuf = resource_manager.queryBuf("vertexbuf");
            vkCmdBindVertexBuffers(cmdbuf[i], 0, 1, &_vertexBuf, &offset);
            VkRect2D scissor = { 0, 0, 800, 800 };
            vkCmdSetScissor(cmdbuf[i], 0, 1, &scissor);
            VkViewport vp = { 0.0, 0.0, 800, 800, 0.0, 1.0 };
            vkCmdSetViewport(cmdbuf[i], 0, 1, &vp);
            vkCmdDraw(cmdbuf[i], 4, 1, 0, 0);

            /* subpass 1 composite */
            vkCmdNextSubpass(cmdbuf[i], VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmdbuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline[SP1]);
            vkCmdBindDescriptorSets(cmdbuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                sp0sp1_pipeline_layout, 0, 1, &descset[SP0], 0, nullptr);
            vkCmdBindVertexBuffers(cmdbuf[i], 0, 1, &_vertexBuf, &offset);
            vkCmdSetScissor(cmdbuf[i], 0, 1, &scissor);
            vkCmdSetViewport(cmdbuf[i], 0, 1, &vp);
            vkCmdDraw(cmdbuf[i], 4, 1, 0, 0);

            vkCmdEndRenderPass(cmdbuf[i]);
            vkEndCommandBuffer(cmdbuf[i]);
        }
    }

    enum {
        SP0 = 0,
        SP1,
        SPMAX,
    };
public:
    TexObj fogsmoke {};
    array<TexObj, SPMAX> RT {};
    VkSampler smp;
    VkSemaphore presentImgFinished;
    VkSemaphore renderImgFinished;
    VkDescriptorSetLayout sp0sp1_descset_layout;
    VkPipelineLayout sp0sp1_pipeline_layout;
    VkDescriptorPool descpool;
    array<VkDescriptorSet, SPMAX> descset;
    array<VkPipeline, SPMAX> pipeline {};
    array<VkVertexInputBindingDescription, 1> vibd;
    array<VkVertexInputAttributeDescription, 2> viad;
};

int main(int argc, char const *argv[])
{
    App app;
    app.run();
    return 0;
}

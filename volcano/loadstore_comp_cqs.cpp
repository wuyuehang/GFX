#include "lava_lite.hpp"
#include <SOIL/SOIL.h>
#include <chrono>

class App : public Volcano {
public:
    ~App() {
        vkQueueWaitIdle(gfxQ);
        vkFreeDescriptorSets(device, descpool, 2, descset);
        vkDestroyDescriptorPool(device, descpool, nullptr);
        vkDestroyDescriptorSetLayout(device, com_descset_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, gfx_descset_layout, nullptr);
        vkDestroyPipelineLayout(device, com_pipeline_layout, nullptr);
        vkDestroyPipelineLayout(device, gfx_pipeline_layout, nullptr);
        vkDestroyPipeline(device, com_pipeline, nullptr);
        vkDestroyPipeline(device, gfx_pipeline, nullptr);

        vkDestroySemaphore(device, swapImgAcquire, nullptr);
        vkDestroySemaphore(device, renderImgFinished, nullptr);
        vkDestroySemaphore(device, computeFinished, nullptr);

        for (const auto iter : fb) {
            vkDestroyFramebuffer(device, iter, nullptr);
        }
        vkDestroyRenderPass(device, renderpass, nullptr);

        vkDestroyImageView(device, srcTexObj.imgv, nullptr);
        vkDestroyImageView(device, dstTexObj.imgv, nullptr);
        vkFreeMemory(device, srcTexObj.memory, nullptr);
        vkFreeMemory(device, dstTexObj.memory, nullptr);
        vkDestroyImage(device, srcTexObj.img, nullptr);
        vkDestroyImage(device, dstTexObj.img, nullptr);

        vkDestroySampler(device, smp, nullptr);
        resource_manager.freeBuf(device);
    }

    App() {
        initBuffer();
        initSampler();
        initLinearTexture2D();
        initRenderPass();
        initFramebuffer();
        initSyncObj();

        initCOMPipeline();
        initGFXPipeline();
        initDescriptor();

        initCOMCommand();
        initGFXCommand();
    }

    void initBuffer() {
        float position[] = {
            -1.0, -1.0, 0.0, 0.0,
            -1.0, 1.0, 0.0, 1.0,
            1.0, -1.0, 1.0, 0.0,
            1.0, 1.0, 1.0, 1.0,
        };

        resource_manager.allocBuf(device, pdmp, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                sizeof(position), position, "vertexbuffer", VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    void initSampler() {
        VkSamplerCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .mipLodBias = 0.0,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 0,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_NEVER,
            .minLod = 0.0,
            .maxLod = 0.0,
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
        };
        vkCreateSampler(device, &info, nullptr, &smp);
    }

    void initLinearTexture2D() {
        int width, height;
        uint8_t *img = SOIL_load_image("ReneDescartes.jpeg", &width, &height, 0, SOIL_LOAD_RGBA);

        bakeImage(srcTexObj, VK_FORMAT_R8G8B8A8_UNORM, width, height, VK_IMAGE_TILING_LINEAR,
            VK_IMAGE_USAGE_STORAGE_BIT, img);

        SOIL_free_image_data(img);

        bakeImage(dstTexObj, VK_FORMAT_R8G8B8A8_UNORM, width, height, VK_IMAGE_TILING_LINEAR,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, nullptr);

        preTransitionImgLayout(srcTexObj.img, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        preTransitionImgLayout(dstTexObj.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }

    void initRenderPass() {
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

    void initFramebuffer() {
        fb.resize(swapchain_imgv.size());

        for (uint32_t i = 0; i < swapchain_imgv.size(); i++) {
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

    void initSyncObj() {
        VkSemaphoreCreateInfo semaInfo {};
        semaInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        vkCreateSemaphore(device, &semaInfo, nullptr, &swapImgAcquire);
        vkCreateSemaphore(device, &semaInfo, nullptr, &renderImgFinished);
        vkCreateSemaphore(device, &semaInfo, nullptr, &computeFinished);
    }

    void initCOMPipeline() {
        VkShaderModule module = initShaderModule("loadstore_comp_cqs.comp.spv");

        VkPipelineShaderStageCreateInfo stageInfo {};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = module;
        stageInfo.pName = "main";

        VkDescriptorSetLayoutBinding bindings[] = {
            [0] = {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .pImmutableSamplers = nullptr,
            },
            [1] = {
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .pImmutableSamplers = nullptr,
            }
        };

        VkDescriptorSetLayoutCreateInfo dsetLayoutInfo {};
        dsetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsetLayoutInfo.bindingCount = 2;
        dsetLayoutInfo.pBindings = bindings;

        vkCreateDescriptorSetLayout(device, &dsetLayoutInfo, nullptr, &com_descset_layout);

        VkPipelineLayoutCreateInfo layoutinfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = 1,
            .pSetLayouts = &com_descset_layout,
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = nullptr,
        };
        vkCreatePipelineLayout(device, &layoutinfo, nullptr, &com_pipeline_layout);

        VkComputePipelineCreateInfo cppInfo = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = stageInfo,
            .layout = com_pipeline_layout,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0
        };
        vkCreateComputePipelines(device, nullptr, 1, &cppInfo, nullptr, &com_pipeline);

        vkDestroyShaderModule(device, module, nullptr);
    }

    void initGFXPipeline() {
        VkShaderModule vertShaderModule = initShaderModule("quad.vert.spv");
        VkShaderModule fragShaderModule = initShaderModule("quad.frag.spv");

        VkPipelineShaderStageCreateInfo shaderStageInfo[2] = {};
        shaderStageInfo[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStageInfo[0].module = vertShaderModule;
        shaderStageInfo[0].pName = "main";

        shaderStageInfo[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStageInfo[1].module = fragShaderModule;
        shaderStageInfo[1].pName = "main";

        /* graphics pipeline -- state */
        VkVertexInputBindingDescription vbd = {
            .binding = 0,
            .stride = 4*sizeof(float),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };

        VkVertexInputAttributeDescription ad[] = {
            [0] = {
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = 0,
            },
            [1] = {
                .location = 1,
                .binding = 0,
                .format = VK_FORMAT_R32G32_SFLOAT,
                .offset = 2*sizeof(float),
            },
        };

        VkPipelineVertexInputStateCreateInfo vertInputInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &vbd,
            .vertexAttributeDescriptionCount = 2,
            .pVertexAttributeDescriptions = ad,
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
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
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
        vkCreateDescriptorSetLayout(device, &dsLayoutInfo, nullptr, &gfx_descset_layout);

        VkPipelineLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = 1,
            .pSetLayouts = &gfx_descset_layout,
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = nullptr
        };
        vkCreatePipelineLayout(device, &layoutInfo, nullptr, &gfx_pipeline_layout);

        VkGraphicsPipelineCreateInfo gfxPipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stageCount = 2,
            .pStages = shaderStageInfo,
            .pVertexInputState = &vertInputInfo,
            .pInputAssemblyState = &iaInfo,
            .pTessellationState = nullptr,
            .pViewportState = &fixfunc_templ.vpsInfo,
            .pRasterizationState = &fixfunc_templ.rstInfo,
            .pMultisampleState = &fixfunc_templ.msaaInfo,
            .pDepthStencilState = &fixfunc_templ.dsInfo,
            .pColorBlendState = &fixfunc_templ.bldInfo,
            .pDynamicState = &fixfunc_templ.dynamicInfo,
            .layout = gfx_pipeline_layout,
            .renderPass = renderpass,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0
        };
        vkCreateGraphicsPipelines(device, nullptr, 1, &gfxPipelineInfo, nullptr, &gfx_pipeline);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
    }

    void initDescriptor() {
        VkDescriptorPoolSize poolSize[] = {
            [0] = {
                .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 2,
            },
            [1] = {
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
            }
        };

        VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 2,
            .poolSizeCount = 2,
            .pPoolSizes = poolSize,
        };
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &descpool);

        VkDescriptorSetLayout descset_layout[] = { com_descset_layout, gfx_descset_layout };
        VkDescriptorSetAllocateInfo ainfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = descpool,
            .descriptorSetCount = 2,
            .pSetLayouts = descset_layout,
        };
        vkAllocateDescriptorSets(device, &ainfo, descset);

        /* Update DescriptorSets */
        VkDescriptorImageInfo descImgInfo[] = {
            [0] = {
                .sampler = nullptr,
                .imageView = srcTexObj.imgv,
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            },
            [1] = {
                .sampler = nullptr,
                .imageView = dstTexObj.imgv,
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            },
            [2] = {
                .sampler = smp,
                .imageView = dstTexObj.imgv,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            }
        };

        VkWriteDescriptorSet wds[] = {
            [0] = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = descset[0],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo = &descImgInfo[0],
                .pBufferInfo = nullptr,
                .pTexelBufferView = nullptr,
            },
            [1] = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = descset[0],
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo = &descImgInfo[1],
                .pBufferInfo = nullptr,
                .pTexelBufferView = nullptr,
            },
            [2] = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = descset[1],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &descImgInfo[2],
                .pBufferInfo = nullptr,
                .pTexelBufferView = nullptr,
            }
        };
        vkUpdateDescriptorSets(device, 3, wds, 0, nullptr);
    }

    void initCOMCommand() {
        VkCommandBufferBeginInfo beginInfo {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        for (uint8_t i = 0; i < swapchain_imgv.size(); i++) {
            VkCommandBuffer & nongfxcmdbuf = cmdbuf[i+2];
            vkBeginCommandBuffer(nongfxcmdbuf, &beginInfo);

            {
                VkImageMemoryBarrier imb = {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .pNext = nullptr,
                    .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = dstTexObj.img,
                    .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    }
                };

                vkCmdPipelineBarrier(nongfxcmdbuf, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imb);
            }

            vkCmdBindPipeline(nongfxcmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, com_pipeline);
            vkCmdBindDescriptorSets(nongfxcmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, com_pipeline_layout, 0, 1, &descset[0], 0, nullptr);
            vkCmdDispatch(nongfxcmdbuf, 1, 800, 1);

            {
                VkImageMemoryBarrier imb = {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .pNext = nullptr,
                    .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = dstTexObj.img,
                    .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    }
                };

                vkCmdPipelineBarrier(nongfxcmdbuf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imb);
            }

            vkEndCommandBuffer(nongfxcmdbuf);
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

            VkClearValue cvs[2] = {};
            cvs[0].color = {0.01, 0.02, 0.0, 1.0};
            cvs[1].depthStencil = { 1.0, 0 };
            rpBeginInfo.clearValueCount = 2;
            rpBeginInfo.pClearValues = cvs;

            vkCmdBeginRenderPass(cmdbuf[i], &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmdbuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS, gfx_pipeline);
            vkCmdBindDescriptorSets(cmdbuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                gfx_pipeline_layout, 0, 1, &descset[1], 0, nullptr);
            VkDeviceSize offset = {};
            VkBuffer _vertexBuf = resource_manager.queryBuf("vertexbuffer");
            vkCmdBindVertexBuffers(cmdbuf[i], 0, 1, &_vertexBuf, &offset);
            VkRect2D scissor = { 10, 10, 780, 780 };
            vkCmdSetScissor(cmdbuf[i], 0, 1, &scissor);
            VkViewport vp = { 0.0, 0.0, 800, 800, 0.0, 1.0 };
            vkCmdSetViewport(cmdbuf[i], 0, 1, &vp);
            vkCmdDraw(cmdbuf[i], 4, 1, 0, 0);
            vkCmdEndRenderPass(cmdbuf[i]);

            vkEndCommandBuffer(cmdbuf[i]);
        }
    }

    void Run() {
        glfwShowWindow(glfw);
        uint32_t ImageIndex = 0;
        while (!glfwWindowShouldClose(glfw)) {
            glfwPollEvents();

            /* presentation engine will block forever until unused images are available */
            vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, swapImgAcquire, VK_NULL_HANDLE, &ImageIndex);

            {
                //VkPipelineStageFlags ws = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                VkSubmitInfo nongfxSubmitInfo = {
                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    .pNext = nullptr,
                    .waitSemaphoreCount = 0,
                    .pWaitSemaphores = nullptr,//&swapImgAcquire,
                    .pWaitDstStageMask = nullptr,
                    .commandBufferCount = 1,
                    .pCommandBuffers = &cmdbuf[ImageIndex + 2],
                    .signalSemaphoreCount = 1,
                    .pSignalSemaphores = &computeFinished,
                };
                vkQueueSubmit(nongfxQ, 1, &nongfxSubmitInfo, VK_NULL_HANDLE);
            }

            {
                VkPipelineStageFlags ws[2] = { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
                VkSemaphore wait_list[2] = { computeFinished, swapImgAcquire };

                VkSubmitInfo gfxSubmitInfo = {
                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    /* stages prior to output color stage can already process while output color stage must
                    * wait until _swpImgAcquire semaphore is signaled.
                    */
                    .pNext = nullptr,
                    .waitSemaphoreCount = 2,
                    .pWaitSemaphores = wait_list,
                    .pWaitDstStageMask = ws,
                    .commandBufferCount = 1,
                    .pCommandBuffers = &cmdbuf[ImageIndex],
                    /* signal finish of render process, status from unsignaled to signaled */
                    .signalSemaphoreCount = 1,
                    .pSignalSemaphores = &renderImgFinished,
                };
                vkQueueSubmit(gfxQ, 1, &gfxSubmitInfo, VK_NULL_HANDLE);
            }

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
            vkQueuePresentKHR(gfxQ, &pi);
        }
    }

public:
    VkSampler smp;
    struct TexObj srcTexObj {};
    struct TexObj dstTexObj {};
    /* sync between presentation engine and start of command execution */
    VkSemaphore swapImgAcquire;
    /* sync between finish of command execution and present request to presentation engine */
    VkSemaphore renderImgFinished;
    VkSemaphore computeFinished;
    VkDescriptorPool descpool;
    VkDescriptorSet descset[2]; /* 0 for compute, 1 for gfx */
    /* gfx */
    VkPipelineLayout gfx_pipeline_layout;
    VkPipeline gfx_pipeline;
    VkDescriptorSetLayout gfx_descset_layout;
    /* compute */
    VkPipelineLayout com_pipeline_layout;
    VkPipeline com_pipeline;
    VkDescriptorSetLayout com_descset_layout;
};

int main(int argc, char const *argv[])
{
    App app;
    app.Run();
    return 0;
}

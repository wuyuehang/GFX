#include "lava_lite.hpp"
#include "controller.hpp"
#include <SOIL/SOIL.h>
#include <glm/glm.hpp>

glm::mat4 MVP_mat;
Spinner spin(800, 800);

void appmousecb(GLFWwindow* win, double xpos, double ypos) {
    spin.onMouseEvent(win, xpos, ypos);
}

void appkeycb(GLFWwindow* win, int key, int scancode, int action, int mode) {
    spin.onKeyEvent(win, key, scancode, action, mode);
}

void appmousebuttoncb(GLFWwindow *win, int button, int action, int mode) {
    spin.onMouseButtonEvent(win, button, action, mode);
}

class App : public Volcano {
public:
    ~App() {
        vkQueueWaitIdle(gfxQ);
        vkFreeDescriptorSets(device, descpool, 1, &gfx_descset);
        vkDestroyDescriptorPool(device, descpool, nullptr);
        vkDestroyDescriptorSetLayout(device, gfx_descset_layout, nullptr);
        vkDestroyPipelineLayout(device, gfx_pipeline_layout, nullptr);
        vkDestroyPipeline(device, gfx_pipeline, nullptr);
        vkDestroySemaphore(device, swapImgAcquire, nullptr);
        vkDestroySemaphore(device, renderImgFinished, nullptr);
        for (const auto iter : fb) {
            vkDestroyFramebuffer(device, iter, nullptr);
        }
        vkDestroyRenderPass(device, renderpass, nullptr);
        vkDestroySampler(device, smp, nullptr);
        vkDestroyImageView(device, srcTexObj.imgv, nullptr);
        vkFreeMemory(device, srcTexObj.memory, nullptr);
        vkDestroyImage(device, srcTexObj.img, nullptr);
        resource_manager.freeBuf(device);
    }

    App() {
        initBuffer();
        initTexture();
        initSampler();
        initRenderpass();
        initFramebuffer();
        initSync();
        initGFXPipeline();
        initDescriptor();
        initGFXCommand();
        glfwSetKeyCallback(glfw, appkeycb);
        glfwSetCursorPosCallback(glfw, appmousecb);
        glfwSetMouseButtonCallback(glfw, appmousebuttoncb);
    }

    void initBuffer() {
        float vertex_data[] = {
            -1.0, 1.0, 0.0, 1.0,
            -1.0, -1.0, 0.0, 0.0,
            1.0, 1.0, 1.0, 1.0,
            1.0, -1.0, 1.0, 0.0
        };

        resource_manager.allocBuf(device, pdmp, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            sizeof(vertex_data), vertex_data, "vertexbuffer", VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        resource_manager.allocBuf(device, pdmp, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                sizeof(glm::mat4), nullptr, "uniformbuf", VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    void initTexture() {
        int width, height;
        uint8_t *img = SOIL_load_image("ReneDescartes.jpeg", &width, &height, 0, SOIL_LOAD_RGBA);

        bakeImage(srcTexObj, VK_FORMAT_R8G8B8A8_UNORM, width, height, VK_IMAGE_TILING_LINEAR,
            VK_IMAGE_USAGE_SAMPLED_BIT, img);

        SOIL_free_image_data(img);

        preTransitionImgLayout(srcTexObj.img, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
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

    void initSync() {
        VkSemaphoreCreateInfo semaInfo {};
        semaInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        vkCreateSemaphore(device, &semaInfo, nullptr, &swapImgAcquire);
        vkCreateSemaphore(device, &semaInfo, nullptr, &renderImgFinished);
    }

    void initGFXPipeline() {
        VkShaderModule vertShaderModule = initShaderModule("quad_separate_sampler.vert.spv");

        VkPipelineShaderStageCreateInfo shaderStageInfo[2] = {};
        shaderStageInfo[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStageInfo[0].module = vertShaderModule;
        shaderStageInfo[0].pName = "main";

        VkShaderModule fragShaderModule = initShaderModule("specialize_constant.frag.spv");
        VkSpecializationMapEntry entry[2] {
            [0] = {
                .constantID = 5,
                .offset = 0,
                .size = 4,
            },
            [1] = {
                .constantID = 7,
                .offset = 4,
                .size = 4,
            }
        };

        uint32_t specialize_datum[2];
        float factor = 0.05;
        specialize_datum[0] = 1;
        specialize_datum[1] = *((uint32_t *)&factor);

        VkSpecializationInfo spInfo {};
        spInfo.mapEntryCount = 2;
        spInfo.pMapEntries = entry;
        spInfo.dataSize = sizeof(specialize_datum);
        spInfo.pData = specialize_datum;

        shaderStageInfo[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStageInfo[1].module = fragShaderModule;
        shaderStageInfo[1].pName = "main";
        shaderStageInfo[1].pSpecializationInfo = &spInfo;

        /* graphics pipeline -- state */
        VkVertexInputBindingDescription vibd = {
            .binding = 0,
            .stride = 4*sizeof(float),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };

        VkVertexInputAttributeDescription viad[] = {
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
            .pVertexBindingDescriptions = &vibd,
            .vertexAttributeDescriptionCount = 2,
            .pVertexAttributeDescriptions = viad,
        };

        VkPipelineInputAssemblyStateCreateInfo iaInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
            .primitiveRestartEnable = VK_FALSE,
        };

        VkDescriptorSetLayoutBinding bindings[] = {
            [0] = {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr,
            },
            [1] = {
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr
            },
            [2] = {
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .pImmutableSamplers = nullptr,
            }
        };

        VkDescriptorSetLayoutCreateInfo dsLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = 3,
            .pBindings = bindings
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
                .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .descriptorCount = 1,
            },
            [1] = {
                .type = VK_DESCRIPTOR_TYPE_SAMPLER,
                .descriptorCount = 1,
            },
            [2] = {
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
            }
        };

        VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 1,
            .poolSizeCount = 3,
            .pPoolSizes = poolSize,
        };
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &descpool);

        VkDescriptorSetAllocateInfo ainfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = descpool,
            .descriptorSetCount = 1,
            .pSetLayouts = &gfx_descset_layout,
        };
        vkAllocateDescriptorSets(device, &ainfo, &gfx_descset);

        /* Update DescriptorSets */
        VkDescriptorImageInfo descImgInfo[] = {
            [0] = {
                .sampler = nullptr,
                .imageView = srcTexObj.imgv,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
            [1] = {
                .sampler = smp,
                .imageView = nullptr,
                .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            }
        };

        VkDescriptorBufferInfo descUniInfo = {
            .buffer = resource_manager.queryBuf("uniformbuf"),
            .offset = 0,
            .range = sizeof(glm::mat4),
        };

        VkWriteDescriptorSet wds[] = {
            [0] = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = gfx_descset,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .pImageInfo = &descImgInfo[0],
                .pBufferInfo = nullptr,
                .pTexelBufferView = nullptr,
            },
            [1] = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = gfx_descset,
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .pImageInfo = &descImgInfo[1],
                .pBufferInfo = nullptr,
                .pTexelBufferView = nullptr,
            },
            [2] = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = gfx_descset,
                .dstBinding = 2,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pImageInfo = nullptr,
                .pBufferInfo = &descUniInfo,
                .pTexelBufferView = nullptr,
            },
        };
        vkUpdateDescriptorSets(device, 3, wds, 0, nullptr);
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
            cvs[0].color = { 0.0, 0.0, 0.0, 1.0 };
            cvs[1].depthStencil = { 1.0, 0 };
            rpBeginInfo.clearValueCount = 2;
            rpBeginInfo.pClearValues = cvs;

            vkCmdBeginRenderPass(cmdbuf[i], &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmdbuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS, gfx_pipeline);
            vkCmdBindDescriptorSets(cmdbuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                gfx_pipeline_layout, 0, 1, &gfx_descset, 0, nullptr);
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

    void run() {
        glfwShowWindow(glfw);
        uint32_t ImageIndex = 0;
        while (!glfwWindowShouldClose(glfw)) {
            glfwPollEvents();

            spin.advance();
            spin.compute();
            MVP_mat = spin.mvp();

            resource_manager.updateBufContent(device, "uniformbuf", &MVP_mat);

            /* presentation engine will block forever until unused images are available */
            vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, swapImgAcquire, VK_NULL_HANDLE, &ImageIndex);

            {
                VkPipelineStageFlags ws[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

                VkSubmitInfo gfxSubmitInfo = {
                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    /* stages prior to output color stage can already process while output color stage must
                    * wait until _swpImgAcquire semaphore is signaled.
                    */
                    .pNext = nullptr,
                    .waitSemaphoreCount = 1,
                    .pWaitSemaphores = &swapImgAcquire,
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

private:
    TexObj srcTexObj {};
    VkSampler smp;
    VkSemaphore swapImgAcquire;
    VkSemaphore renderImgFinished;
    VkDescriptorSetLayout gfx_descset_layout;
    VkPipelineLayout gfx_pipeline_layout;
    VkPipeline gfx_pipeline;
    VkDescriptorPool descpool;
    VkDescriptorSet gfx_descset;
};

int main(int argc, char const *argv[])
{
    App app;
    app.run();
    return 0;
}

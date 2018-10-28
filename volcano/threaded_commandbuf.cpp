#include "lava_lite.hpp"
#include <SOIL/SOIL.h>
#include <pthread.h>
#include <string>

class App;
struct thread_workload {
    App *app;
    uint32_t token;
};

void *thread_worker(void *arg);

class App : public Volcano {
public:
    ~App() {
        vkQueueWaitIdle(gfxQ);

        for (uint32_t i = 0; i < 4; i++) {
            vkFreeCommandBuffers(device, threadcmdpool[i], 1, &threadcmdbuf[i]);
            vkDestroyCommandPool(device, threadcmdpool[i], nullptr);
        }

        vkDestroyDescriptorSetLayout(device, gfx_descset_layout, nullptr);
        vkDestroyPipelineLayout(device, gfx_pipeline_layout, nullptr);
        vkDestroyPipeline(device, gfx_pipeline, nullptr);
        vkDestroySemaphore(device, presentImgFinished, nullptr);
        vkDestroySemaphore(device, renderImgFinished, nullptr);
        for (const auto iter : fb) {
            vkDestroyFramebuffer(device, iter, nullptr);
        }
        vkDestroyRenderPass(device, renderpass, nullptr);
        resource_manager.freeBuf(device);
    }

    App() {
        initBuffer();
        initRenderpass();
        initFramebuffer();
        initSync();
        initGFXPipeline();
        initThread();
        initGFXCommand();
    }

    void initBuffer() {
        float vertex_data[] = {
            0.0, 1.0, -1.0, 1.0, -1.0, 0.0,
            -1.0, 0.0, -1.0, -1.0, 0.0, -1.0,
            0.0, -1.0, 1.0, -1.0, 1.0, 0.0,
            1.0, 0.0, 1.0, 1.0, 0.0, 1.0,
        };

        resource_manager.allocBuf(device, pdmp, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            sizeof(vertex_data), vertex_data, "vertexbuf", VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
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

        vkCreateSemaphore(device, &semaInfo, nullptr, &presentImgFinished);
        vkCreateSemaphore(device, &semaInfo, nullptr, &renderImgFinished);
    }

    void initGFXPipeline() {
        VkShaderModule vertShaderModule = initShaderModule("single_attribute_nonmvp.vert.spv");
        VkShaderModule fragShaderModule = initShaderModule("constant.frag.spv");

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
        VkVertexInputBindingDescription vibd = {
            .binding = 0,
            .stride = 2*sizeof(float),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };

        VkVertexInputAttributeDescription viad = {
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
            .pVertexBindingDescriptions = &vibd,
            .vertexAttributeDescriptionCount = 1,
            .pVertexAttributeDescriptions = &viad,
        };

        VkPipelineInputAssemblyStateCreateInfo iaInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        };

        VkDescriptorSetLayoutCreateInfo dsLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = 0,
            .pBindings = nullptr,
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

    void initThread() {
        for (uint32_t i = 0; i < 4; i++) {
            wl[i].app = this;
            wl[i].token = i;

            assert(0 == pthread_create(&thread[i], nullptr, thread_worker, &wl[i]));
            std::string name = "worker_" + std::to_string(0);
            pthread_setname_np(thread[i], name.c_str());
        }

        for (uint32_t i = 0; i < 4; i++) {
            pthread_join(thread[i], nullptr);
        }
    }

    void initThreadedCommandbuf(uint32_t token) {
        VkCommandPoolCreateInfo pinfo {};
        pinfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pinfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pinfo.queueFamilyIndex = 0;
        vkCreateCommandPool(device, &pinfo, nullptr, &threadcmdpool[token]);

        VkCommandBufferAllocateInfo ainfo {};
        ainfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ainfo.commandPool = threadcmdpool[token];
        ainfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        ainfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(device, &ainfo, &threadcmdbuf[token]);

        VkCommandBufferInheritanceInfo inheritanceInfo {};
        inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
        inheritanceInfo.renderPass = renderpass;
        inheritanceInfo.subpass = 0;
        inheritanceInfo.framebuffer = VK_NULL_HANDLE;
        inheritanceInfo.occlusionQueryEnable = VK_FALSE;

        VkCommandBufferBeginInfo cbbi {};
        cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbbi.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        cbbi.pInheritanceInfo = &inheritanceInfo;

        vkBeginCommandBuffer(threadcmdbuf[token], &cbbi);
        vkCmdBindPipeline(threadcmdbuf[token], VK_PIPELINE_BIND_POINT_GRAPHICS, gfx_pipeline);
        VkDeviceSize offset = token * 24;
        VkBuffer _vertexBuf = resource_manager.queryBuf("vertexbuf");
        vkCmdBindVertexBuffers(threadcmdbuf[token], 0, 1, &_vertexBuf, &offset);
        VkRect2D scissor = { 10, 10, 780, 780 };
        vkCmdSetScissor(threadcmdbuf[token], 0, 1, &scissor);
        VkViewport vp = { 0.0, 0.0, 800, 800, 0.0, 1.0 };
        vkCmdSetViewport(threadcmdbuf[token], 0, 1, &vp);
        vkCmdDraw(threadcmdbuf[token], 3, 1, 0, 0);
        vkEndCommandBuffer(threadcmdbuf[token]);
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

            vkCmdBeginRenderPass(cmdbuf[i], &rpBeginInfo, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
            vkCmdExecuteCommands(cmdbuf[i], 4, threadcmdbuf);
            vkCmdEndRenderPass(cmdbuf[i]);

            vkEndCommandBuffer(cmdbuf[i]);
        }
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
                    /* stages prior to output color stage can already process while output color stage must
                    * wait until presentImgFinished semaphore is signaled.
                    */
                    .pNext = nullptr,
                    .waitSemaphoreCount = 1,
                    .pWaitSemaphores = &presentImgFinished,
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
    VkSemaphore presentImgFinished;
    VkSemaphore renderImgFinished;
    VkDescriptorSetLayout gfx_descset_layout;
    VkPipelineLayout gfx_pipeline_layout;
    VkPipeline gfx_pipeline;
    VkCommandPool threadcmdpool[4];
    VkCommandBuffer threadcmdbuf[4];
    pthread_t thread[4];
    struct thread_workload wl[4]; // separate workload is so important
};

void *thread_worker(void *arg) {
    struct thread_workload *wl = nullptr;
    wl = static_cast<thread_workload *>(arg);
    wl->app->initThreadedCommandbuf(wl->token);

    return nullptr;
}

int main(int argc, char const *argv[])
{
    App app;
    app.run();
    return 0;
}

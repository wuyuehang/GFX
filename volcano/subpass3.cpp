#include "lava_lite.hpp"
#include <SOIL/SOIL.h>

class App : public Volcano {
public:
    ~App() {
        vkQueueWaitIdle(gfxQ);

        for (uint32_t i = 0; i < SPMAX; i++) {
            vkFreeDescriptorSets(device, descpool, 1, &descset[i]);
            vkDestroyPipeline(device, pipeline[i], nullptr);
        }
        vkDestroyDescriptorPool(device, descpool, nullptr);
        vkDestroyDescriptorSetLayout(device, sp2_descset_layout, nullptr);
        vkDestroyPipelineLayout(device, sp2_pipeline_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, sp0sp1_descset_layout, nullptr);
        vkDestroyPipelineLayout(device, sp0sp1_pipeline_layout, nullptr);

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
        vkDestroyImageView(device, portrait.imgv, nullptr);
        vkFreeMemory(device, fogsmoke.memory, nullptr);
        vkFreeMemory(device, portrait.memory, nullptr);
        vkDestroyImage(device, fogsmoke.img, nullptr);
        vkDestroyImage(device, portrait.img, nullptr);
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
        uint8_t *img = SOIL_load_image("ReneDescartes.jpeg", &width, &height, 0, SOIL_LOAD_RGBA);

        bakeImage(portrait, VK_FORMAT_R8G8B8A8_UNORM, width, height, VK_IMAGE_TILING_LINEAR,
            VK_IMAGE_USAGE_SAMPLED_BIT, img);

        SOIL_free_image_data(img);

        preTransitionImgLayout(portrait.img, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        img = SOIL_load_image("green-fog-smoke.jpg", &width, &height, 0, SOIL_LOAD_RGBA);

        bakeImage(fogsmoke, VK_FORMAT_R8G8B8A8_UNORM, width, height, VK_IMAGE_TILING_LINEAR,
            VK_IMAGE_USAGE_SAMPLED_BIT, img);

        SOIL_free_image_data(img);

        preTransitionImgLayout(fogsmoke.img, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }

    void initSubpassRenderTarget() {
        bakeImage(RT[SP0], VK_FORMAT_R8G8B8A8_UNORM,
            surfacecapkhr.currentExtent.width, surfacecapkhr.currentExtent.height,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, nullptr);

        preTransitionImgLayout(RT[SP0].img,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

        bakeImage(RT[SP1], VK_FORMAT_R8G8B8A8_UNORM,
            surfacecapkhr.currentExtent.width, surfacecapkhr.currentExtent.height,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, nullptr);

        preTransitionImgLayout(RT[SP1].img,
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
        array<VkAttachmentReference, 4> attRef {};
        attRef[0].attachment = 0;
        attRef[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attRef[1].attachment = 1;
        attRef[1].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attRef[2].attachment = 2;
        attRef[2].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attRef[3].attachment = 3;
        attRef[3].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        array<VkAttachmentDescription, 4> attDesc {};
        /* swapchain image */
        attDesc[0].format = surfacefmtkhr[0].format;
        attDesc[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attDesc[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        /* depth image */
        attDesc[1].format = VK_FORMAT_D32_SFLOAT;
        attDesc[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attDesc[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        /* subpass 0 render target */
        attDesc[2].format = VK_FORMAT_R8G8B8A8_UNORM;
        attDesc[2].samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc[2].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attDesc[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        /* subpass 1 render target */
        attDesc[3].format = VK_FORMAT_R8G8B8A8_UNORM;
        attDesc[3].samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc[3].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attDesc[3].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        array<VkSubpassDescription, 3> spDesc {};
        /* subpass 0 : portrait process */
        spDesc[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        spDesc[0].inputAttachmentCount = 0;
        spDesc[0].pInputAttachments = nullptr;
        spDesc[0].colorAttachmentCount = 1;
        spDesc[0].pColorAttachments = &attRef[2];
        spDesc[0].pResolveAttachments = nullptr;
        spDesc[0].pDepthStencilAttachment = nullptr;
        spDesc[0].preserveAttachmentCount = 0;
        spDesc[0].pPreserveAttachments = nullptr;
        /* subpass 1 : fogsmoke process */
        spDesc[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        spDesc[1].inputAttachmentCount = 0;
        spDesc[1].pInputAttachments = nullptr;
        spDesc[1].colorAttachmentCount = 1;
        spDesc[1].pColorAttachments = &attRef[3];
        spDesc[1].pResolveAttachments = nullptr;
        spDesc[1].pDepthStencilAttachment = nullptr;
        spDesc[1].preserveAttachmentCount = 0;
        spDesc[1].pPreserveAttachments = nullptr;
        /* subpass 2 : composite */
        spDesc[2].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        array<VkAttachmentReference, 2> sp2_inpute {};
        sp2_inpute[0] = {
            .attachment = 2,
            .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        sp2_inpute[1] = {
            .attachment = 3,
            .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        spDesc[2].inputAttachmentCount = sp2_inpute.size();
        spDesc[2].pInputAttachments = sp2_inpute.data();
        spDesc[2].colorAttachmentCount = 1;
        spDesc[2].pColorAttachments = &attRef[0];
        spDesc[2].pResolveAttachments = nullptr;
        spDesc[2].pDepthStencilAttachment = &attRef[1];
        spDesc[2].preserveAttachmentCount = 0;
        spDesc[2].pPreserveAttachments = nullptr;

        array<VkSubpassDependency, 5> subpassDep {};
        /* outside world to subpass 0 */
        subpassDep[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        subpassDep[0].dstSubpass = SP0;
        subpassDep[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        subpassDep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        subpassDep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        subpassDep[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        /* outside world to subpass 1 */
        subpassDep[1].srcSubpass = VK_SUBPASS_EXTERNAL;
        subpassDep[1].dstSubpass = SP1;
        subpassDep[1].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        subpassDep[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep[1].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        subpassDep[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        subpassDep[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        /* subpass 0 to subpass 2 */
        subpassDep[2].srcSubpass = SP0;
        subpassDep[2].dstSubpass = SP2;
        subpassDep[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        subpassDep[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        subpassDep[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        subpassDep[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        /* subpass 1 to subpass 2 */
        subpassDep[3].srcSubpass = SP1;
        subpassDep[3].dstSubpass = SP2;
        subpassDep[3].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep[3].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        subpassDep[3].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        subpassDep[3].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        subpassDep[3].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        /* subpass 2 to outside world */
        subpassDep[4].srcSubpass = SP2;
        subpassDep[4].dstSubpass = VK_SUBPASS_EXTERNAL;
        subpassDep[4].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep[4].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        subpassDep[4].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        subpassDep[4].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        subpassDep[4].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

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
            array<VkImageView, 4> att {};
            att[0] = swapchain_imgv[i];
            att[1] = depth_imgv;
            att[2] = RT[SP0].imgv;
            att[3] = RT[SP1].imgv;

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

        {
            /* subpass 2 */
            array<VkDescriptorSetLayoutBinding, 2> bindings {};
            bindings[0].binding = 0;
            bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings[0].pImmutableSamplers = nullptr;
            bindings[1].binding = 1;
            bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings[1].pImmutableSamplers = nullptr;

            VkDescriptorSetLayoutCreateInfo dsLayoutInfo = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .bindingCount = bindings.size(),
                .pBindings = bindings.data(),
            };
            vkCreateDescriptorSetLayout(device, &dsLayoutInfo, nullptr, &sp2_descset_layout);

            VkPipelineLayoutCreateInfo layoutInfo = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .setLayoutCount = 1,
                .pSetLayouts = &sp2_descset_layout,
                .pushConstantRangeCount = 0,
                .pPushConstantRanges = nullptr
            };
            vkCreatePipelineLayout(device, &layoutInfo, nullptr, &sp2_pipeline_layout);
        }
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

        gfxPipelineInfo.layout = sp0sp1_pipeline_layout;
        gfxPipelineInfo.subpass = 1;
        vkCreateGraphicsPipelines(device, nullptr, 1, &gfxPipelineInfo, nullptr, &pipeline[SP1]);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        {
            /* subpass 2 */
            VkShaderModule vertShaderModule = initShaderModule("quad.vert.spv");
            VkShaderModule fragShaderModule = initShaderModule("subpass3.frag.spv");

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
                .layout = sp2_pipeline_layout,
                .renderPass = renderpass,
                .subpass = 2,
                .basePipelineHandle = VK_NULL_HANDLE,
                .basePipelineIndex = 0
            };
            vkCreateGraphicsPipelines(device, nullptr, 1, &gfxPipelineInfo, nullptr, &pipeline[SP2]);
            vkDestroyShaderModule(device, vertShaderModule, nullptr);
            vkDestroyShaderModule(device, fragShaderModule, nullptr);
        }
    }

    void initDescriptor() {
        array<VkDescriptorPoolSize, 2> poolSize {};
        poolSize[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize[0].descriptorCount = 2;
        poolSize[1].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        poolSize[1].descriptorCount = 2;

        VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 3,
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
            descImgInfo[0].imageView = portrait.imgv;
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
        {
            VkDescriptorSetAllocateInfo ainfo = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .pNext = nullptr,
                .descriptorPool = descpool,
                .descriptorSetCount = 1,
                .pSetLayouts = &sp2_descset_layout,
            };
            vkAllocateDescriptorSets(device, &ainfo, &descset[SP2]);

            array<VkDescriptorImageInfo, 2> descImgInfo {};
            descImgInfo[0].sampler = smp;
            descImgInfo[0].imageView = RT[SP0].imgv;
            descImgInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            descImgInfo[1].sampler = smp;
            descImgInfo[1].imageView = RT[SP1].imgv;
            descImgInfo[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            array<VkWriteDescriptorSet, 2> wds {};
            wds[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wds[0].pNext = nullptr;
            wds[0].dstSet = descset[SP2];
            wds[0].dstBinding = 0;
            wds[0].dstArrayElement = 0;
            wds[0].descriptorCount = 1;
            wds[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            wds[0].pImageInfo = &descImgInfo[0];
            wds[0].pBufferInfo = nullptr;
            wds[0].pTexelBufferView = nullptr;
            wds[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wds[1].pNext = nullptr;
            wds[1].dstSet = descset[SP2];
            wds[1].dstBinding = 1;
            wds[1].dstArrayElement = 0;
            wds[1].descriptorCount = 1;
            wds[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            wds[1].pImageInfo = &descImgInfo[1];
            wds[1].pBufferInfo = nullptr;
            wds[1].pTexelBufferView = nullptr;
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

            array<VkClearValue, 4> cvs = {};
            cvs[0].color = { 0.0, 0.0, 0.0, 1.0 };
            cvs[1].depthStencil = { 1.0, 0 };
            cvs[2].color = { 0.0, 0.0, 0.0, 1.0 };
            cvs[3].color = { 0.0, 0.0, 0.0, 1.0 };
            rpBeginInfo.clearValueCount = cvs.size();
            rpBeginInfo.pClearValues = cvs.data();

            vkCmdBeginRenderPass(cmdbuf[i], &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
            /* portrait */
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
            /* fogsmoke */
            vkCmdNextSubpass(cmdbuf[i], VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmdbuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline[SP1]);
            vkCmdBindDescriptorSets(cmdbuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                sp0sp1_pipeline_layout, 0, 1, &descset[SP1], 0, nullptr);
            vkCmdBindVertexBuffers(cmdbuf[i], 0, 1, &_vertexBuf, &offset);
            vkCmdSetScissor(cmdbuf[i], 0, 1, &scissor);
            vkCmdSetViewport(cmdbuf[i], 0, 1, &vp);
            vkCmdDraw(cmdbuf[i], 4, 1, 0, 0);
            /* subpass 2 */
            vkCmdNextSubpass(cmdbuf[i], VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmdbuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline[SP2]);
            vkCmdBindDescriptorSets(cmdbuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                sp2_pipeline_layout, 0, 1, &descset[SP2], 0, nullptr);
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
        SP2,
        SPMAX,
    };
public:
    TexObj portrait {};
    TexObj fogsmoke {};
    array<TexObj, SPMAX> RT {};
    VkSampler smp;
    VkSemaphore presentImgFinished;
    VkSemaphore renderImgFinished;
    VkDescriptorSetLayout sp2_descset_layout;
    VkDescriptorSetLayout sp0sp1_descset_layout;
    VkDescriptorPool descpool;
    array<VkDescriptorSet, SPMAX> descset;
    VkPipelineLayout sp2_pipeline_layout;
    VkPipelineLayout sp0sp1_pipeline_layout;
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

#include "lava_lite.hpp"
#include "controller.hpp"
#include <SOIL/SOIL.h>
#include <glm/glm.hpp>

glm::mat4 MVP_mat;
glm::vec3 camloc = glm::vec3(0, 0, 5);
glm::vec3 camfront = glm::vec3(0, 0, -1);
glm::vec3 camup = glm::vec3(0, 1, 0);

CameraRoam cam(800, 800, camloc, camup, camfront);

void appmousecb(GLFWwindow* win, double xpos, double ypos) {
    cam.onMouseEvent(win, xpos, ypos);
}

void appkeycb(GLFWwindow* win, int key, int scancode, int action, int mode) {
    cam.onKeyEvent(win, key, scancode, action, mode);
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
        initGFXPipeline();
        initDescriptor();
        initGFXCommand();
        glfwSetKeyCallback(glfw, appkeycb);
        glfwSetCursorPosCallback(glfw, appmousecb);
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

        bakeImage(srcTexObj, VK_FORMAT_R8G8B8A8_UNORM, width, height,
            VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_SAMPLED_BIT, img);

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
        array<VkAttachmentReference, 2> attRef {};
        attRef[0].attachment = 0;
        attRef[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attRef[1].attachment = 1;
        attRef[1].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        array<VkAttachmentDescription, 2> attDesc {};
        attDesc[0].format = surfacefmtkhr[0].format;
        attDesc[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attDesc[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        attDesc[1].format = VK_FORMAT_D32_SFLOAT;
        attDesc[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attDesc[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        array<VkSubpassDescription, 1> spDesc {};
        spDesc[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        spDesc[0].inputAttachmentCount = 0;
        spDesc[0].pInputAttachments = nullptr;
        spDesc[0].colorAttachmentCount = 1;
        spDesc[0].pColorAttachments = &attRef[0];
        spDesc[0].pResolveAttachments = nullptr;
        spDesc[0].pDepthStencilAttachment = &attRef[1];
        spDesc[0].preserveAttachmentCount = 0;
        spDesc[0].pPreserveAttachments = nullptr;

        array<VkSubpassDependency, 2> subpassDep {};
        subpassDep[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        subpassDep[0].dstSubpass = 0;
        subpassDep[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        subpassDep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        subpassDep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        subpassDep[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        subpassDep[1].srcSubpass = 0;
        subpassDep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        subpassDep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        subpassDep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        subpassDep[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        subpassDep[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

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

    void initGFXPipeline() {
        VkShaderModule vertShaderModule = initShaderModule("quad_separate_sampler.vert.spv");
        VkShaderModule fragShaderModule = initShaderModule("quad_separate_sampler.frag.spv");

        array<VkPipelineShaderStageCreateInfo, 2> shaderStageInfo = {};
        shaderStageInfo[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStageInfo[0].module = vertShaderModule;
        shaderStageInfo[0].pName = "main";

        shaderStageInfo[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStageInfo[1].module = fragShaderModule;
        shaderStageInfo[1].pName = "main";

        array<VkVertexInputBindingDescription, 1> vibd = {};
        vibd[0].binding = 0;
        vibd[0].stride = 4*sizeof(float);
        vibd[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        array<VkVertexInputAttributeDescription, 2> viad = {};
        viad[0].location = 0;
        viad[0].binding = 0;
        viad[0].format = VK_FORMAT_R32G32_SFLOAT;
        viad[0].offset = 0;
        viad[1].location = 1;
        viad[1].binding = 0;
        viad[1].format = VK_FORMAT_R32G32_SFLOAT;
        viad[1].offset = 2*sizeof(float);

        VkPipelineVertexInputStateCreateInfo vertInputInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = vibd.size(),
            .pVertexBindingDescriptions = vibd.data(),
            .vertexAttributeDescriptionCount = viad.size(),
            .pVertexAttributeDescriptions = viad.data(),
        };

        VkPipelineInputAssemblyStateCreateInfo iaInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
            .primitiveRestartEnable = VK_FALSE,
        };

        array<VkDescriptorSetLayoutBinding, 3> bindings = {};
        bindings[0] = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        };
        bindings[1] = {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr
        };
        bindings[2] = {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .pImmutableSamplers = nullptr,
        };

        VkDescriptorSetLayoutCreateInfo dsLayoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = bindings.size(),
            .pBindings = bindings.data(),
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
        array<VkDescriptorPoolSize, 3> poolSize = {};
        poolSize[0] = {
            .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = 1,
        };
        poolSize[1] = {
            .type = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = 1,
        };
        poolSize[2] = {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
        };

        VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 1,
            .poolSizeCount = poolSize.size(),
            .pPoolSizes = poolSize.data(),
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

        array<VkDescriptorImageInfo, 2> descImgInfo = {};
        descImgInfo[0] = {
            .sampler = nullptr,
            .imageView = srcTexObj.imgv,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        descImgInfo[1] = {
            .sampler = smp,
            .imageView = nullptr,
            .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        array<VkDescriptorBufferInfo, 1> descUniInfo {};
        descUniInfo[0].buffer = resource_manager.queryBuf("uniformbuf");
        descUniInfo[0].offset = 0;
        descUniInfo[0].range = sizeof(glm::mat4);

        array<VkWriteDescriptorSet, 3> wds = {};
        wds[0] = {
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
        };
        wds[1] = {
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
        };
        wds[2] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = gfx_descset,
            .dstBinding = 2,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pImageInfo = nullptr,
            .pBufferInfo = &descUniInfo[0],
            .pTexelBufferView = nullptr,
        };
        vkUpdateDescriptorSets(device, wds.size(), wds.data(), 0, nullptr);
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

    void drawFrame() override {
        cam.advance();
        cam.compute();
        MVP_mat = cam.mvp();
        resource_manager.updateBufContent(device, "uniformbuf", &MVP_mat);
    }

private:
    TexObj srcTexObj {};
    VkSampler smp;
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

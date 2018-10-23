#include "lava_offscreen_lite.hpp"

class App : public Volcano {
public:
    ~App() {
        vkFreeCommandBuffers(device, cmdpool, 16, seccmd);
        vkFreeDescriptorSets(device, descpool, 1, &gfx_descset);
        vkDestroyDescriptorPool(device, descpool, nullptr);
        vkDestroyDescriptorSetLayout(device, gfx_descset_layout, nullptr);

        vkDestroyPipelineLayout(device, gfx_pipeline_layout, nullptr);
        vkDestroyPipeline(device, gfx_pipeline, nullptr);

        resource_manager.freeBuf(device);
        vkDestroySampler(device, smp, nullptr);
    }

    App() = delete;
    App(uint32_t w, uint32_t h) : Volcano(w, h) {
        initBuffer();
        initTexture();
        initSampler();
        initGFXPipeline();
        initDescriptor();
        initSecondaryCommand();
        initGFXCommand(w, h);
        run(w, h);
    }

    void initBuffer() {
        float position[] = {
            -1.0, -1.0, 0.0, 0.0,
            -1.0, 1.0, 0.0, 1.0,
            1.0, -1.0, 1.0, 0.0,
            1.0, 1.0, 1.0, 1.0,
        };

        resource_manager.allocBuf(device, pdmp, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                sizeof(position), position, "vertexbuf", VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    void initTexture() {
        int width, height;
        uint8_t *img = SOIL_load_image("mayon-volcano-erupt.jpg", &width, &height, 0, SOIL_LOAD_RGBA);

        VkImageCreateInfo imgInfo {};
        imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imgInfo.extent.width = width;
        imgInfo.extent.height = height;
        imgInfo.extent.depth = 1;
        imgInfo.mipLevels = 1;
        imgInfo.arrayLayers = 1;
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling = VK_IMAGE_TILING_LINEAR;
        imgInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
        vkCreateImage(device, &imgInfo, nullptr, &texObj.img);

        VkMemoryRequirements req {};
        vkGetImageMemoryRequirements(device, texObj.img, &req);

        VkMemoryAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = req.size;
        allocInfo.memoryTypeIndex = resource_manager.findProperties(&pdmp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device, &allocInfo, nullptr, &texObj.mem);

        vkBindImageMemory(device, texObj.img, texObj.mem, 0);

        VkImageViewCreateInfo imgViewInfo {};
        imgViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imgViewInfo.image = texObj.img;
        imgViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imgViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imgViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imgViewInfo.subresourceRange.baseMipLevel = 0;
        imgViewInfo.subresourceRange.levelCount = 1;
        imgViewInfo.subresourceRange.baseArrayLayer = 0;
        imgViewInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &imgViewInfo, nullptr, &texObj.imgv);

        VkImageSubresource subresource {};
        subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource.mipLevel = 0;
        subresource.arrayLayer = 0;

        VkSubresourceLayout subresource_layout = {};
        vkGetImageSubresourceLayout(device, texObj.img, &subresource, &subresource_layout);

        uint8_t *pDST = nullptr;

        vkMapMemory(device, texObj.mem, 0, req.size, 0, (void **)&pDST);
        for (int i = 0; i < height; i++) {
            memcpy(pDST, (img + width * i * 4), width * 4);
            pDST = pDST + subresource_layout.rowPitch;
        }
        vkUnmapMemory(device, texObj.mem);

        SOIL_free_image_data(img);

        preTransitionImgLayout(texObj.img,
            VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        texDustbin.push_back(texObj);
    }

    void initSampler() {
        VkSamplerCreateInfo smpInfo = {};
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

    void initGFXPipeline() {
        VkShaderModule vertShaderModule = initShaderModule("quad.vert.spv");
        VkShaderModule fragShaderModule = initShaderModule("quad.frag.spv");

        VkPipelineShaderStageCreateInfo shaderStageInfos[2] {};
        shaderStageInfos[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStageInfos[0].module = vertShaderModule;
        shaderStageInfos[0].pName = "main";
        shaderStageInfos[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStageInfos[1].module = fragShaderModule;
        shaderStageInfos[1].pName = "main";

        /* graphics pipeline -- state */
        VkVertexInputBindingDescription vibd {};
        vibd.binding = 0;
        vibd.stride = 4*sizeof(float);
        vibd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription viad[2] {};
        viad[0].location = 0;
        viad[0].binding = 0;
        viad[0].format = VK_FORMAT_R32G32_SFLOAT;
        viad[0].offset = 0;
        viad[1].location = 1;
        viad[1].binding = 0;
        viad[1].format = VK_FORMAT_R32G32_SFLOAT;
        viad[1].offset = 2*sizeof(float);

        VkPipelineVertexInputStateCreateInfo vertInputInfo = {};
        vertInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertInputInfo.vertexBindingDescriptionCount = 1;
        vertInputInfo.pVertexBindingDescriptions = &vibd;
        vertInputInfo.vertexAttributeDescriptionCount = 2;
        vertInputInfo.pVertexAttributeDescriptions = viad;

        VkPipelineInputAssemblyStateCreateInfo iaInfo = {};
        iaInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        iaInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        iaInfo.primitiveRestartEnable = VK_FALSE;

        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo dsLayoutInfo = {};
        dsLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsLayoutInfo.bindingCount = 1;
        dsLayoutInfo.pBindings = &binding;
        vkCreateDescriptorSetLayout(device, &dsLayoutInfo, nullptr, &gfx_descset_layout);

        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &gfx_descset_layout;
        vkCreatePipelineLayout(device, &layoutInfo, nullptr, &gfx_pipeline_layout);

        VkGraphicsPipelineCreateInfo gfxPipelineInfo = {};
        gfxPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gfxPipelineInfo.stageCount = 2;
        gfxPipelineInfo.pStages = shaderStageInfos;
        gfxPipelineInfo.pVertexInputState = &vertInputInfo;
        gfxPipelineInfo.pInputAssemblyState = &iaInfo;
        gfxPipelineInfo.pViewportState = &fixfunc_templ.vpsInfo;
        gfxPipelineInfo.pRasterizationState = &fixfunc_templ.rstInfo;
        gfxPipelineInfo.pMultisampleState = &fixfunc_templ.msaaInfo;
        gfxPipelineInfo.pDepthStencilState = &fixfunc_templ.dsInfo;
        gfxPipelineInfo.pColorBlendState = &fixfunc_templ.bldInfo;
        gfxPipelineInfo.pDynamicState = &fixfunc_templ.dynamicInfo;
        gfxPipelineInfo.layout = gfx_pipeline_layout;
        gfxPipelineInfo.renderPass = renderpass;
        gfxPipelineInfo.subpass = 0;
        vkCreateGraphicsPipelines(device, nullptr, 1, &gfxPipelineInfo, nullptr, &gfx_pipeline);

        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
    }

    void initDescriptor() {
        VkDescriptorPoolSize poolSize = {};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo dsPoolInfo = {};
        dsPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dsPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        dsPoolInfo.maxSets = 1;
        dsPoolInfo.poolSizeCount = 1;
        dsPoolInfo.pPoolSizes = &poolSize;
        vkCreateDescriptorPool(device, &dsPoolInfo, nullptr, &descpool);

        VkDescriptorSetAllocateInfo dsAllocInfo = {};
        dsAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAllocInfo.descriptorPool = descpool;
        dsAllocInfo.descriptorSetCount = 1;
        dsAllocInfo.pSetLayouts = &gfx_descset_layout;
        vkAllocateDescriptorSets(device, &dsAllocInfo, &gfx_descset);

        /* Update DescriptorSets */
        VkDescriptorImageInfo descImgInfo = {};
        descImgInfo.sampler = smp;
        descImgInfo.imageView = texObj.imgv;
        descImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet wds = {};
        wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wds.dstSet = gfx_descset;
        wds.dstBinding = 0;
        wds.dstArrayElement = 0;
        wds.descriptorCount = 1;
        wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wds.pImageInfo = &descImgInfo;

        vkUpdateDescriptorSets(device, 1, &wds, 0, nullptr);
    }

    void initSecondaryCommand() {
        VkCommandBufferAllocateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.commandPool = cmdpool;
        info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        info.commandBufferCount = 16;
        vkAllocateCommandBuffers(device, &info, seccmd);

        VkCommandBufferInheritanceInfo inheritanceInfo {};
        inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
        inheritanceInfo.renderPass = renderpass;
        inheritanceInfo.subpass = 0;
        inheritanceInfo.framebuffer = VK_NULL_HANDLE;
        inheritanceInfo.occlusionQueryEnable = VK_FALSE;

        VkCommandBufferBeginInfo cbbi {};
        cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        cbbi.pInheritanceInfo = &inheritanceInfo;

        VkBuffer _vertexBuf = resource_manager.queryBuf("vertexbuf");
        VkDeviceSize offset = {};

        for (uint8_t i = 0; i < 16; i++) {
            vkBeginCommandBuffer(seccmd[i], &cbbi);

            vkCmdBindPipeline(seccmd[i], VK_PIPELINE_BIND_POINT_GRAPHICS, gfx_pipeline);
            vkCmdBindDescriptorSets(seccmd[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                gfx_pipeline_layout, 0, 1, &gfx_descset, 0, nullptr);

            vkCmdBindVertexBuffers(seccmd[i], 0, 1, &_vertexBuf, &offset);

            VkRect2D scissor = { 0, 0, 800, 800 };
            vkCmdSetScissor(seccmd[i], 0, 1, &scissor);
            VkViewport vp = { float(i%4*200), float(i/4%4*200), 800 / 4, 800 / 4, 0.0, 1.0 };
            vkCmdSetViewport(seccmd[i], 0, 1, &vp);

            vkCmdDraw(seccmd[i], 4, 1, 0, 0);

            vkEndCommandBuffer(seccmd[i]);
        }
    }

    void initGFXCommand(uint32_t w, uint32_t h) {
        VkCommandBuffer & rendercmdbuf = cmdbuf[0];
        VkCommandBufferBeginInfo cbi = {};
        cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbi.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        vkBeginCommandBuffer(rendercmdbuf, &cbi);

        VkRenderPassBeginInfo rpBeginInfo = {};
        rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBeginInfo.renderPass = renderpass;
        rpBeginInfo.framebuffer = fb;
        rpBeginInfo.renderArea.offset = {0, 0};
        rpBeginInfo.renderArea.extent.width = w;
        rpBeginInfo.renderArea.extent.height = h;

        VkClearValue cvs[2] = {};
        cvs[0].color = {0.0, 0.0, 0.0, 1.0};
        cvs[1].depthStencil = { 1.0, 0 };
        rpBeginInfo.clearValueCount = 2;
        rpBeginInfo.pClearValues = cvs;

        vkCmdBeginRenderPass(rendercmdbuf, &rpBeginInfo, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
        vkCmdExecuteCommands(rendercmdbuf, 16, seccmd);
        vkCmdEndRenderPass(rendercmdbuf);

        vkEndCommandBuffer(rendercmdbuf);
    }

    void run(uint32_t w, uint32_t h) {
        VkFence fence;
        VkFenceCreateInfo fenceInfo {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        vkCreateFence(device, &fenceInfo, nullptr, &fence);

        VkSubmitInfo si {};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmdbuf[0];

        vkQueueSubmit(gfxQ, 1, &si, fence);

        VkResult res = VK_SUCCESS;
        do {
            res = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        } while (res == VK_TIMEOUT);
        assert(res == VK_SUCCESS);

        vkDestroyFence(device, fence, nullptr);

        diskRTImage("output.tga", w, h);
    }

private:
    TexObj texObj;
    VkSampler smp;
    VkDescriptorSetLayout gfx_descset_layout;
    VkPipelineLayout gfx_pipeline_layout;
    VkPipeline gfx_pipeline;
    VkDescriptorPool descpool;
    VkDescriptorSet gfx_descset;
    VkCommandBuffer seccmd[16];
};

int main(int argc, char const *argv[])
{
    App app = App(800, 800);
    return 0;
}

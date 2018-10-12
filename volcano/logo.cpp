#include <SOIL/SOIL.h>
#include "volcano.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class App : public Volcano {
public:
    ~App() {
        vkQueueWaitIdle(queue);

        vkDestroyPipelineCache(device, pplcache, nullptr);

        vkFreeDescriptorSets(device, descPool, 1, &descSet);
        vkDestroyDescriptorPool(device, descPool, nullptr);
        vkDestroyDescriptorSetLayout(device, descSetLayout, nullptr);

        vkDestroyPipelineLayout(device, layout, nullptr);
        vkDestroyPipeline(device, gfxPipeline, nullptr);

        resource_manager.freeBuf(device);
        vkDestroySampler(device, smp, nullptr);
        vkDestroyImageView(device, tx2d_imgv, nullptr);
        vkFreeMemory(device, tx2d_mem, nullptr);
        vkDestroyImage(device, tx2d_img, nullptr);;
    }

    App() {
        InitBuffers();
        BakeTexture2D();
        InitSampler();
        _bakePipelineCache("logo_pipeline_cache.bin", pplcache);
        InitGFXPipeline();
        //_diskPipelineCache("logo_pipeline_cache.bin", pplcache);
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
            cvs[0].color = {0.05, 0.0, 0.0, 1.0};
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
        uint8_t *img = SOIL_load_image("mayon-volcano-erupt.jpg", &width, &height, 0, SOIL_LOAD_RGBA);

        VkBuffer srcBuf = resource_manager.allocBuf(device, pdmp, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            width*height*4, img, "stagingbuffer", VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        SOIL_free_image_data(img);

        VkImageCreateInfo imgInfo = {};
        imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imgInfo.extent.width = width;
        imgInfo.extent.height = height;
        imgInfo.extent.depth = 1;
        imgInfo.mipLevels = 1;
        imgInfo.arrayLayers = 1;
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkCreateImage(device, &imgInfo, nullptr, &tx2d_img);

        VkMemoryRequirements req = {};
        vkGetImageMemoryRequirements(device, tx2d_img, &req);

        VkMemoryAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = req.size;
        allocInfo.memoryTypeIndex = resource_manager.findProperties(&pdmp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device, &allocInfo, nullptr, &tx2d_mem);

        vkBindImageMemory(device, tx2d_img, tx2d_mem, 0);

        VkImageViewCreateInfo imgViewInfo = {};
        imgViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imgViewInfo.image = tx2d_img;
        imgViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imgViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imgViewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        imgViewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        imgViewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        imgViewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        imgViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imgViewInfo.subresourceRange.baseMipLevel = 0;
        imgViewInfo.subresourceRange.levelCount = 1;
        imgViewInfo.subresourceRange.baseArrayLayer = 0;
        imgViewInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &imgViewInfo, nullptr, &tx2d_imgv);

        /* transition texture layout */
        VkCommandBuffer transitionCMD = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo transitionCMDAllocInfo {};
        transitionCMDAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        transitionCMDAllocInfo.commandPool = rendercmdpool;
        transitionCMDAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        transitionCMDAllocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(device, &transitionCMDAllocInfo, &transitionCMD);

        VkCommandBufferBeginInfo cbi = {};
        cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(transitionCMD, &cbi);

        VkImageMemoryBarrier imb = {};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imb.srcAccessMask = 0;
        imb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imb.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.image = tx2d_img;
        imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imb.subresourceRange.baseMipLevel = 0;
        imb.subresourceRange.levelCount = 1;
        imb.subresourceRange.baseArrayLayer = 0;
        imb.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(transitionCMD, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imb);

        VkBufferImageCopy rg = {};
        rg.bufferOffset = 0;
        rg.bufferRowLength = 0;
        rg.bufferImageHeight = 0;
        rg.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        rg.imageSubresource.mipLevel = 0;
        rg.imageSubresource.baseArrayLayer = 0;
        rg.imageSubresource.layerCount = 1;
        rg.imageOffset = VkOffset3D{ 0, 0, 0 };
        rg.imageExtent = VkExtent3D{ uint32_t(width), uint32_t(height), 1 };
        vkCmdCopyBufferToImage(transitionCMD, srcBuf, tx2d_img,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);

        imb = {};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imb.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.image = tx2d_img;
        imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imb.subresourceRange.baseMipLevel = 0;
        imb.subresourceRange.levelCount = 1;
        imb.subresourceRange.baseArrayLayer = 0;
        imb.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(transitionCMD, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imb);

        vkEndCommandBuffer(transitionCMD);
        VkSubmitInfo si = {};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &transitionCMD;
        vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);
        vkFreeCommandBuffers(device, rendercmdpool, 1, &transitionCMD);
    }

    void InitSampler() {
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

    void InitBuffers() {
        float position[] = {
            -1.0, -1.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0,
            -1.0, 1.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0,
            1.0, -1.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 0.0,
            1.0, 1.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0,
        };

        resource_manager.allocBuf(device, pdmp, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                sizeof(position), position, "vertexbuffer", VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        glm::mat4 view_mat = glm::lookAt(
            glm::vec3(1, -1, 1),
            glm::vec3(0, 0, 0),
            glm::vec3(0, 1, 0)
        );
        glm::mat4 model_mat = glm::scale(glm::mat4(1.0), glm::vec3(0.5f, 0.5f, 0.5f));
        glm::mat4 proj_mat = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 10.0f);

        MVP_mat = proj_mat * view_mat * model_mat;
        resource_manager.allocBuf(device, pdmp, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                sizeof(MVP_mat), &MVP_mat, "mvp_uniform_buf", VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    void InitGFXPipeline() {
        /* graphics pipeline -- shader */
        VkShaderModule vertShaderModule = VK_NULL_HANDLE;
        auto vert = loadSPIRV("triple_attribute.vert.spv");
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
        auto frag = loadSPIRV("dual_attribute.frag.spv");
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
        VkVertexInputBindingDescription vbd = {};
        vbd.binding = 0;
        vbd.stride = 9*sizeof(float);
        vbd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription ad[3] = {};
        ad[0].location = 0;
        ad[0].binding = 0;
        ad[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        ad[0].offset = 0;
        ad[1].location = 1;
        ad[1].binding = 0;
        ad[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        ad[1].offset = 3*sizeof(float);
        ad[2].location = 2;
        ad[2].binding = 0;
        ad[2].format = VK_FORMAT_R32G32_SFLOAT;
        ad[2].offset = 7*sizeof(float);

        VkPipelineVertexInputStateCreateInfo vertInputInfo = {};
        vertInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertInputInfo.vertexBindingDescriptionCount = 1;
        vertInputInfo.pVertexBindingDescriptions = &vbd;
        vertInputInfo.vertexAttributeDescriptionCount = 3;
        vertInputInfo.pVertexAttributeDescriptions = ad;

        VkPipelineInputAssemblyStateCreateInfo iaInfo = {};
        iaInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        iaInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        iaInfo.primitiveRestartEnable = VK_FALSE;

        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        VkDescriptorSetLayoutBinding bindings[2] = {};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo dsLayoutInfo = {};
        dsLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsLayoutInfo.bindingCount = 2;
        dsLayoutInfo.pBindings = bindings;
        vkCreateDescriptorSetLayout(device, &dsLayoutInfo, nullptr, &descSetLayout);

        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descSetLayout;

        vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout);

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
        gfxPipelineInfo.layout = layout;
        gfxPipelineInfo.renderPass = renderpass;
        gfxPipelineInfo.subpass = 0;

        vkCreateGraphicsPipelines(device, pplcache, 1, &gfxPipelineInfo, nullptr, &gfxPipeline);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
    }

    void InitDescriptor() {
        VkDescriptorPoolSize poolSize[2] = {};
        poolSize[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize[0].descriptorCount = 1;
        poolSize[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize[1].descriptorCount = 1;

        VkDescriptorPoolCreateInfo dsPoolInfo = {};
        dsPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dsPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        dsPoolInfo.maxSets = 1;
        dsPoolInfo.poolSizeCount = 2;
        dsPoolInfo.pPoolSizes = poolSize;
        vkCreateDescriptorPool(device, &dsPoolInfo, nullptr, &descPool);

        VkDescriptorSetAllocateInfo dsAllocInfo = {};
        dsAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAllocInfo.descriptorPool = descPool;
        dsAllocInfo.descriptorSetCount = 1;
        dsAllocInfo.pSetLayouts = &descSetLayout;
        vkAllocateDescriptorSets(device, &dsAllocInfo, &descSet);

        /* Update DescriptorSets */
        VkDescriptorImageInfo descImgInfo = {};
        descImgInfo.sampler = smp;
        descImgInfo.imageView = tx2d_imgv;
        descImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorBufferInfo descUniInfo = {};
        descUniInfo.buffer = resource_manager.queryBuf("mvp_uniform_buf");
        descUniInfo.offset = 0;
        descUniInfo.range = sizeof(glm::mat4);

        VkWriteDescriptorSet wds[2] = {};
        wds[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wds[0].dstSet = descSet;
        wds[0].dstBinding = 0;
        wds[0].dstArrayElement = 0;
        wds[0].descriptorCount = 1;
        wds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wds[0].pImageInfo = &descImgInfo;

        wds[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wds[1].dstSet = descSet;
        wds[1].dstBinding = 1;
        wds[1].dstArrayElement = 0;
        wds[1].descriptorCount = 1;
        wds[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        wds[1].pBufferInfo = &descUniInfo;

        vkUpdateDescriptorSets(device, 2, wds, 0, nullptr);
    }

public:
    VkImage tx2d_img;
    VkDeviceMemory tx2d_mem;
    VkImageView tx2d_imgv;
    VkSampler smp;
    /* pipeline */
    VkPipelineLayout layout;
    VkPipeline gfxPipeline;
    VkPipelineCache pplcache;
    /* descrpitor */
    VkDescriptorSetLayout descSetLayout;
    VkDescriptorPool descPool;
    VkDescriptorSet descSet;
    glm::mat4 MVP_mat;
};

int main(int argc, char const *argv[])
{
    App app;
    app.Run();
    return 0;
}

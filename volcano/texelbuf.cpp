#include <SOIL/SOIL.h>
#include "volcano.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class App : public Volcano {
public:
    ~App() {
        vkQueueWaitIdle(queue);

        vkFreeDescriptorSets(device, descPool, 1, &descSet);
        vkDestroyDescriptorPool(device, descPool, nullptr);
        vkDestroyDescriptorSetLayout(device, descSetLayout, nullptr);

        vkDestroyPipelineLayout(device, layout, nullptr);
        vkDestroyPipeline(device, gfxPipeline, nullptr);

        vkDestroyBufferView(device, texelbufview, nullptr);
        resource_manager.freeBuf(device);
    }

    App() {
        InitBuffers();
        BakeTexelBuf();
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

    void BakeTexelBuf() {
        int width, height;
        uint8_t *img = SOIL_load_image("ReneDescartes.jpeg", &width, &height, 0, SOIL_LOAD_RGBA);
        assert(width == 800 && height == 800);

        resource_manager.allocBuf(device, pdmp, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
            width*height*4, img, "texelbuf", VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VkBufferViewCreateInfo bufViewInfo = {};
        bufViewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
        bufViewInfo.buffer = resource_manager.queryBuf("texelbuf");
        bufViewInfo.format = VK_FORMAT_R8_UNORM;
        bufViewInfo.offset = 0;
        bufViewInfo.range = width*height*4;
        vkCreateBufferView(device, &bufViewInfo, nullptr, &texelbufview);
    }

    void InitBuffers() {
        float position[] = {
            -1.0, -1.0, 0.0,
            -1.0, 1.0, 0.0,
            1.0, -1.0, 0.0,
            1.0, 1.0, 0.0,
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
        auto vert = loadSPIRV("single_attribute.vert.spv");
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
        auto frag = loadSPIRV("texelbuf.frag.spv");
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
        vbd.stride = 3*sizeof(float);
        vbd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription ad = {};
        ad.location = 0;
        ad.binding = 0;
        ad.format = VK_FORMAT_R32G32B32_SFLOAT;
        ad.offset = 0;

        VkPipelineVertexInputStateCreateInfo vertInputInfo = {};
        vertInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertInputInfo.vertexBindingDescriptionCount = 1;
        vertInputInfo.pVertexBindingDescriptions = &vbd;
        vertInputInfo.vertexAttributeDescriptionCount = 1;
        vertInputInfo.pVertexAttributeDescriptions = &ad;

        VkPipelineInputAssemblyStateCreateInfo iaInfo = {};
        iaInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        iaInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        iaInfo.primitiveRestartEnable = VK_FALSE;

        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        VkDescriptorSetLayoutBinding bindings[2] = {};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
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

        vkCreateGraphicsPipelines(device, nullptr, 1, &gfxPipelineInfo, nullptr, &gfxPipeline);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
    }

    void InitDescriptor() {
        VkDescriptorPoolSize poolSize[2] = {};
        poolSize[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
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
        wds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        wds[0].pTexelBufferView = &texelbufview;

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
    VkBufferView texelbufview;
    /* pipeline */
    VkPipelineLayout layout;
    VkPipeline gfxPipeline;
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

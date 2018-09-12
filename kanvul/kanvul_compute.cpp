#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <fstream>

using std::cout;
using std::vector;
using std::string;
using std::ifstream;

class KanVul {
public:
    KanVul() {
        InitInstance();
        InitPhysicalDevice();
        InitDevice();
        InitCmdPool();
        InitCmdBuffers();
    }

    ~KanVul() {
        vkFreeCommandBuffers(_dev, _cmdpool, 1, &_cmdbuf);
        vkDestroyCommandPool(_dev, _cmdpool, nullptr);
        vkDestroyDevice(_dev, nullptr);
        vkDestroyInstance(_inst, nullptr);
    }

    vector<char> loadSPIRV(const string& filename) {
        ifstream f(filename, std::ios::ate | std::ios::binary);
        size_t filesize = (size_t) f.tellg();
        vector<char> buffer(filesize);
        f.seekg(0);
        f.read(buffer.data(), filesize);
        f.close();

        return buffer;
    }

    void InitInstance() {
        VkApplicationInfo appInfo {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "KanVul_compute";
        appInfo.pEngineName = "KanVul_compute";
        appInfo.apiVersion = VK_API_VERSION_1_1;

        VkInstanceCreateInfo instInfo {};
        instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instInfo.pApplicationInfo = &appInfo;
        vkCreateInstance(&instInfo, nullptr, &_inst);
    }

    void InitPhysicalDevice() {
        uint32_t physicalDeviceCount = 0;
        vkEnumeratePhysicalDevices(_inst, &physicalDeviceCount, nullptr);
        _pdev.resize(physicalDeviceCount);
        vkEnumeratePhysicalDevices(_inst, &physicalDeviceCount, _pdev.data());

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(_pdev[0], &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamily;
        queueFamily.resize(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(_pdev[0], &queueFamilyCount, queueFamily.data());
    }

    void InitDevice() {
        VkDeviceQueueCreateInfo queueInfo {};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = 0;
        queueInfo.queueCount = 1;
        float priority = 1.0;
        queueInfo.pQueuePriorities = &priority;

        VkDeviceCreateInfo deviceInfo {};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        vkCreateDevice(_pdev[0], &deviceInfo, nullptr, &_dev);

        vkGetDeviceQueue(_dev, 0, 0, &_queue);
    }

    void InitCmdPool() {
        VkCommandPoolCreateInfo cmdPoolInfo {};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cmdPoolInfo.queueFamilyIndex = 0;
        vkCreateCommandPool(_dev, &cmdPoolInfo, nullptr, &_cmdpool);
    }

    void InitCmdBuffers() {
        VkCommandBufferAllocateInfo cmdBufferInfo {};
        cmdBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufferInfo.commandPool = _cmdpool;
        cmdBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufferInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(_dev, &cmdBufferInfo, &_cmdbuf);
    }

    void Run() {
        /* set up destination buffer for compute shader write */
        VkBuffer dst_buf;
        VkBufferCreateInfo bufInfo {};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = 256;
        bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(_dev, &bufInfo, nullptr, &dst_buf);

        VkMemoryRequirements memReq {};
        vkGetBufferMemoryRequirements(_dev, dst_buf, &memReq);

        VkMemoryAllocateInfo memInfo {};
        memInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memInfo.allocationSize = memReq.size;
        memInfo.memoryTypeIndex = 0;

        VkDeviceMemory dst_mem;
        vkAllocateMemory(_dev, &memInfo, nullptr, &dst_mem);

        uint8_t *pDST = nullptr;
        vkMapMemory(_dev, dst_mem, 0, memReq.size, 0, (void **)&pDST);
        for (int32_t i = 0; i < 256; i++) {
            *(pDST + i) = 0xAA;
        }
        vkUnmapMemory(_dev, dst_mem);

        vkBindBufferMemory(_dev, dst_buf, dst_mem, 0);

        /* Load spir-v IR codes and init VkShaderModule */
        auto comm_spv = loadSPIRV("kanvul_compute.spv");
        VkShaderModule shaderModule = VK_NULL_HANDLE;
        VkShaderModuleCreateInfo shaderModuleInfo {};
        shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderModuleInfo.pNext = nullptr;
        shaderModuleInfo.flags = 0;
        shaderModuleInfo.codeSize = comm_spv.size();
        shaderModuleInfo.pCode = (const uint32_t *)comm_spv.data();
        vkCreateShaderModule(_dev, &shaderModuleInfo, nullptr, &shaderModule);

        /* Wrap VkShaderModule into VkPipelineShadreStage */
        VkPipelineShaderStageCreateInfo shaderStageInfo {};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.pNext = nullptr;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = shaderModule;
        shaderStageInfo.pName = "main";
        shaderStageInfo.pSpecializationInfo = nullptr;

        /* Set up SSBO VkDescriptorSetLayoutBinding.
         * bind destination buffer for write.
         */
        VkDescriptorSetLayoutBinding SSBObindings;
        SSBObindings.binding = 0;
        SSBObindings.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        SSBObindings.descriptorCount = 1;
        SSBObindings.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        SSBObindings.pImmutableSamplers = nullptr;

        /* Wrap bindings into VkDescriptorSetLayout */
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo {};
        descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutInfo.pNext = nullptr;
        descriptorSetLayoutInfo.flags = 0;
        descriptorSetLayoutInfo.bindingCount = 1;
        descriptorSetLayoutInfo.pBindings = &SSBObindings;
        vkCreateDescriptorSetLayout(_dev, &descriptorSetLayoutInfo, nullptr, &descriptorSetLayout);

        /* Create VkDescriptorPool and allocate vkDescriptorSets */
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDescriptorPoolSize poolSize {};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo descriptorPoolInfo {};
        descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolInfo.pNext = nullptr;
        descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        descriptorPoolInfo.maxSets = 1;
        descriptorPoolInfo.poolSizeCount =1;
        descriptorPoolInfo.pPoolSizes = &poolSize;
        vkCreateDescriptorPool(_dev, &descriptorPoolInfo, nullptr, &descriptorPool);

        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkDescriptorSetAllocateInfo descriptorSetInfo {};
        descriptorSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetInfo.pNext = nullptr;
        descriptorSetInfo.descriptorPool = descriptorPool;
        descriptorSetInfo.descriptorSetCount = 1;
        descriptorSetInfo.pSetLayouts = &descriptorSetLayout;
        vkAllocateDescriptorSets(_dev, &descriptorSetInfo, &descriptorSet);

        /* Update DescriptorSets */
        VkDescriptorBufferInfo descBufferInfo {};
        descBufferInfo.buffer = dst_buf;
        descBufferInfo.offset = 0;
        descBufferInfo.range = memReq.size;

        VkWriteDescriptorSet wds {};
        wds.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wds.pNext = nullptr;
        wds.dstSet = descriptorSet;
        wds.dstBinding = 0;
        wds.dstArrayElement = 0;
        wds.descriptorCount = 1;
        wds.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wds.pImageInfo = nullptr;
        wds.pBufferInfo = &descBufferInfo;
        wds.pTexelBufferView = nullptr;
        vkUpdateDescriptorSets(_dev, 1, &wds, 0, nullptr);

        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipelineLayoutCreateInfo pipelineLayoutInfo {};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.pNext = nullptr;
        pipelineLayoutInfo.flags = 0;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        pipelineLayoutInfo.pPushConstantRanges = nullptr;
        vkCreatePipelineLayout(_dev, &pipelineLayoutInfo, nullptr, &pipelineLayout);

        /* set up compute pipeline */
        VkPipeline pipeline;
        VkComputePipelineCreateInfo pipelineInfo {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = nullptr;
        pipelineInfo.flags = 0;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineInfo.basePipelineIndex = 0;
        vkCreateComputePipelines(_dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

        /* record command buffer */
        VkCommandBufferBeginInfo beginInfo {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(_cmdbuf, &beginInfo);
        vkCmdBindPipeline(_cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(_cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        vkCmdDispatch(_cmdbuf, 1, 1, 1);
        vkEndCommandBuffer(_cmdbuf);

        /* submit command buffer */
        VkSubmitInfo submitInfo {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &_cmdbuf;

        vkQueueSubmit(_queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(_queue);

        /* verify */
        pDST = nullptr;
        vkMapMemory(_dev, dst_mem, 0, memReq.size, 0, (void **)&pDST);
        for (int32_t i = 0; i < 256; i++) {
            cout << int(*(pDST + i)) << (((i+1) % 16) == 0 ? "\n" : ", ");
        }
        vkUnmapMemory(_dev, dst_mem);

        /* clean up */
        vkDestroyShaderModule(_dev, shaderModule, nullptr);

        vkDestroyDescriptorSetLayout(_dev, descriptorSetLayout, nullptr);
        vkFreeDescriptorSets(_dev, descriptorPool, 1, &descriptorSet);
        vkDestroyDescriptorPool(_dev, descriptorPool, nullptr);

        vkDestroyPipelineLayout(_dev, pipelineLayout, nullptr);
        vkDestroyPipeline(_dev, pipeline, nullptr);

        vkFreeMemory(_dev, dst_mem, nullptr);
        vkDestroyBuffer(_dev, dst_buf, nullptr);
    }
private:
    VkInstance _inst;
    vector<VkPhysicalDevice> _pdev;
    VkDevice _dev;
    VkQueue _queue;
    VkCommandPool _cmdpool;
    VkCommandBuffer _cmdbuf;
};

int main(int argc, char const *argv[])
{
    KanVul app;
    app.Run();

    return 0;
}

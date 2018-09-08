#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>

using std::cout;
using std::vector;

class KanVul {
public:
    ~KanVul() {}
    KanVul() {}

    void InitVulkanCore() {
        InitInstance();
        InitPhysicalDevice();
        InitDevice();
        InitCmdPool();
        InitCmdBuffers();
    }

    void DestroyVulkanCore() {
        vkFreeCommandBuffers(_dev, _cmdpool, 1, &_transfer_cmdbuf);
        vkDestroyCommandPool(_dev, _cmdpool, nullptr);
        vkDestroyDevice(_dev, nullptr);
        vkDestroyInstance(_inst, nullptr);
    }

    void InitInstance() {
        VkApplicationInfo ai {};
        ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pNext = nullptr;
        ai.pApplicationName = "KanVul_copybuffer";
        ai.applicationVersion = 0;
        ai.pEngineName = "KanVul_copybuffer";
        ai.engineVersion = 0;
        ai.apiVersion = VK_API_VERSION_1_1;

        VkInstanceCreateInfo ii {};
        ii.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ii.pNext = nullptr;
        ii.pApplicationInfo = &ai;
        ii.enabledLayerCount = 0;
        ii.ppEnabledLayerNames = nullptr;
        ii.enabledExtensionCount = 0;
        ii.ppEnabledExtensionNames = nullptr;

        vkCreateInstance(&ii, nullptr, &_inst);
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
        VkDeviceQueueCreateInfo qi {};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.pNext = nullptr;
        qi.flags = 0;
        qi.queueFamilyIndex = 0;
        qi.queueCount = 1;
        float priority = 1.0;
        qi.pQueuePriorities = &priority;

        VkDeviceCreateInfo di;
        di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        di.pNext = nullptr;
        di.flags = 0;
        di.queueCreateInfoCount = 1;
        di.pQueueCreateInfos = &qi;
        di.enabledLayerCount = 0;
        di.ppEnabledLayerNames = nullptr;
        di.enabledExtensionCount = 0;
        di.ppEnabledExtensionNames = nullptr;
        di.pEnabledFeatures = nullptr;

        vkCreateDevice(_pdev[0], &di, nullptr, &_dev);
    }

    void InitCmdPool(void) {
        vkGetDeviceQueue(_dev, 0, 0, &_queue);

        VkCommandPoolCreateInfo ci {};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.pNext = nullptr;
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = 0;

        vkCreateCommandPool(_dev, &ci, nullptr, &_cmdpool);
    }

    void InitCmdBuffers(void) {
        /* transfer command buffer */
        VkCommandBufferAllocateInfo xfcmdi {};
        xfcmdi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        xfcmdi.pNext = nullptr;
        xfcmdi.commandPool = _cmdpool;
        xfcmdi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        xfcmdi.commandBufferCount = 1;

        vkAllocateCommandBuffers(_dev, &xfcmdi, &_transfer_cmdbuf);
    }

    void Run(void) {
        /* set up source and destination buffer for blit */
        VkBuffer src_buf, dst_buf;
        VkBufferCreateInfo bi {};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.pNext = nullptr;
        bi.pNext = 0;
        bi.size = 256;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bi.queueFamilyIndexCount = 0;
        bi.pQueueFamilyIndices = nullptr;

        vkCreateBuffer(_dev, &bi, nullptr, &src_buf);
        vkCreateBuffer(_dev, &bi, nullptr, &dst_buf);

        VkMemoryRequirements memreq {};
        vkGetBufferMemoryRequirements(_dev, src_buf, &memreq);

        VkMemoryAllocateInfo mi {};
        mi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mi.pNext = nullptr;
        mi.allocationSize = memreq.size;
        mi.memoryTypeIndex = 0;

        VkDeviceMemory src_mem, dst_mem;
        vkAllocateMemory(_dev, &mi, nullptr, &src_mem);
        vkAllocateMemory(_dev, &mi, nullptr, &dst_mem);

        uint8_t *pSRC = nullptr;
        uint8_t *pDST = nullptr;
        vkMapMemory(_dev, src_mem, 0, memreq.size, 0, (void **)&pSRC);
        vkMapMemory(_dev, dst_mem, 0, memreq.size, 0, (void **)&pDST);
        for (int32_t i = 0; i < 256; i++) {
            *(pSRC + i) = i;
            *(pDST + i) = 0xAA;
        }
        vkUnmapMemory(_dev, dst_mem);
        vkUnmapMemory(_dev, src_mem);

        vkBindBufferMemory(_dev, src_buf, src_mem, 0);
        vkBindBufferMemory(_dev, dst_buf, dst_mem, 0);

        /* record command buffer */
        VkCommandBufferBeginInfo cbi {};
        cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbi.pNext = nullptr;
        cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        cbi.pInheritanceInfo = nullptr;

        vkBeginCommandBuffer(_transfer_cmdbuf, &cbi);

        VkBufferCopy region {};
        region.srcOffset = 0;
        region.dstOffset = 0;
        region.size = memreq.size;

        vkCmdCopyBuffer(_transfer_cmdbuf, src_buf, dst_buf, 1, &region);

        vkEndCommandBuffer(_transfer_cmdbuf);
        /* submit command buffer */
        VkSubmitInfo si {};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.pNext = nullptr;
        si.waitSemaphoreCount = 0;
        si.pWaitSemaphores = nullptr;
        si.pWaitDstStageMask = nullptr;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &_transfer_cmdbuf;
        si.signalSemaphoreCount = 0;
        si.pSignalSemaphores = nullptr;

        vkQueueSubmit(_queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(_queue);

        /* varify */
        pDST = nullptr;
        vkMapMemory(_dev, dst_mem, 0, memreq.size, 0, (void **)&pDST);
        for (int32_t i = 0; i < 256; i++) {
            cout << int(*(pDST + i)) << (((i+1) % 16) == 0 ? "\n" : ", ");
        }
        vkUnmapMemory(_dev, dst_mem);

        /* clean up */
        vkFreeMemory(_dev, dst_mem, nullptr);
        vkFreeMemory(_dev, src_mem, nullptr);
        vkDestroyBuffer(_dev, dst_buf, nullptr);
        vkDestroyBuffer(_dev, src_buf, nullptr);
    }
public:
    /* vulkan core */
    VkInstance _inst;
    vector<VkPhysicalDevice> _pdev;
    VkDevice _dev;
    VkQueue _queue;
    VkCommandPool _cmdpool;
    VkCommandBuffer _transfer_cmdbuf;
};

int main(int argc, char const *argv[])
{
    KanVul app;
    app.InitVulkanCore();
    app.Run();
    app.DestroyVulkanCore();

    return 0;
}

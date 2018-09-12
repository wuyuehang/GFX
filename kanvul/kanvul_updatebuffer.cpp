#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>

using std::cout;
using std::vector;

class KanVul {
public:
    ~KanVul() {
        vkFreeCommandBuffers(_dev, _cmdpool, 1, &_transfer_cmdbuf);
        vkDestroyCommandPool(_dev, _cmdpool, nullptr);
        vkDestroyDevice(_dev, nullptr);
        vkDestroyInstance(_inst, nullptr);
    }

    KanVul() {
        InitInstance();
        InitPhysicalDevice();
        InitDevice();
        InitCmdPool();
        InitCmdBuffers();
    }

    void InitInstance() {
        VkApplicationInfo ai {};
        ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pApplicationName = "KanVul_updatebuffer";
        ai.pEngineName = "KanVul_updatebuffer";
        ai.apiVersion = VK_API_VERSION_1_1;

        VkInstanceCreateInfo ii {};
        ii.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ii.pApplicationInfo = &ai;

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
        qi.queueFamilyIndex = 0;
        qi.queueCount = 1;
        float priority = 1.0;
        qi.pQueuePriorities = &priority;

        VkDeviceCreateInfo di {};
        di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        di.pNext = nullptr;
        di.queueCreateInfoCount = 1;
        di.pQueueCreateInfos = &qi;

        vkCreateDevice(_pdev[0], &di, nullptr, &_dev);
    }

    void InitCmdPool() {
        vkGetDeviceQueue(_dev, 0, 0, &_queue);

        VkCommandPoolCreateInfo ci {};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.pNext = nullptr;
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = 0;

        vkCreateCommandPool(_dev, &ci, nullptr, &_cmdpool);
    }

    void InitCmdBuffers() {
        /* transfer command buffer */
        VkCommandBufferAllocateInfo xfcmdi {};
        xfcmdi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        xfcmdi.pNext = nullptr;
        xfcmdi.commandPool = _cmdpool;
        xfcmdi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        xfcmdi.commandBufferCount = 1;

        vkAllocateCommandBuffers(_dev, &xfcmdi, &_transfer_cmdbuf);
    }

    void Run() {
        /* set up buffer for fill */
        VkBuffer dst_buf;
        VkBufferCreateInfo bi {};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.pNext = nullptr;
        bi.pNext = 0;
        bi.size = 256;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bi.queueFamilyIndexCount = 0;
        bi.pQueueFamilyIndices = nullptr;

        vkCreateBuffer(_dev, &bi, nullptr, &dst_buf);

        VkMemoryRequirements memreq {};
        vkGetBufferMemoryRequirements(_dev, dst_buf, &memreq);

        VkMemoryAllocateInfo mi {};
        mi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mi.pNext = nullptr;
        mi.allocationSize = memreq.size;
        mi.memoryTypeIndex = 0;

        VkDeviceMemory dst_mem;
        vkAllocateMemory(_dev, &mi, nullptr, &dst_mem);

        uint8_t *pDST = nullptr;
        vkMapMemory(_dev, dst_mem, 0, memreq.size, 0, (void **)&pDST);

        for (int32_t i = 0; i < 256; i++) {
            *(pDST + i) = i;
        }

        vkUnmapMemory(_dev, dst_mem);

        vkBindBufferMemory(_dev, dst_buf, dst_mem, 0);

        /* record command buffer */
        VkCommandBufferBeginInfo cbi {};
        cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbi.pNext = nullptr;
        cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        cbi.pInheritanceInfo = nullptr;

        vkBeginCommandBuffer(_transfer_cmdbuf, &cbi);

        vkCmdFillBuffer(_transfer_cmdbuf, dst_buf, 0, memreq.size, 0x04030201);

        uint8_t block_at64[64];
        uint8_t block_at128[64];
        for (int i = 0; i < 64; i++) {
            block_at64[i] = 0x8;
            block_at128[i] = 0x9;
        }

        vkCmdUpdateBuffer(_transfer_cmdbuf, dst_buf, 64, 64, block_at64);
        vkCmdUpdateBuffer(_transfer_cmdbuf, dst_buf, 128, 64, block_at128);

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

        /* verify */
        pDST = nullptr;
        vkMapMemory(_dev, dst_mem, 0, memreq.size, 0, (void **)&pDST);
        for (int32_t i = 0; i < 256; i++) {
            cout << int(*(pDST + i)) << (((i+1) % 16) == 0 ? "\n" : ", ");
        }
        vkUnmapMemory(_dev, dst_mem);

        /* clean up */
        vkFreeMemory(_dev, dst_mem, nullptr);
        vkDestroyBuffer(_dev, dst_buf, nullptr);
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
    app.Run();

    return 0;
}

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>

using std::vector;
using std::cout;

class KanVul {
public:
    ~KanVul() {}
    KanVul() {}

    void InitVulkanCore() {
        InitInstance();
        InitPhysicalDevice();
        InitDevice();
    }

    void DestroyVulkanCore() {
        vkDestroyDevice(_dev, nullptr);
        vkDestroyInstance(_inst, nullptr);
    }

    void InitInstance() {
        VkApplicationInfo ai {};
        ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pNext = nullptr;
        ai.pApplicationName = "KanVul_init";
        ai.applicationVersion = 0;
        ai.pEngineName = "KanVul_init";
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

private:
    /* vulkan core */
    VkInstance _inst;
    vector<VkPhysicalDevice> _pdev;
    VkDevice _dev;
};

int main(int argc, char const *argv[])
{
    KanVul app;
    app.InitVulkanCore();
    app.DestroyVulkanCore();

    return 0;
}

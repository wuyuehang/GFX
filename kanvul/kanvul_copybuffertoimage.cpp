#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <cassert>

using std::vector;

class KanVul {
public:
    ~KanVul() {
        vkQueueWaitIdle(_queue);

        vkDestroySemaphore(_dev, _swpImgAcquire, nullptr);
        vkFreeCommandBuffers(_dev, _cmdpool, _cmdbuf.size(), _cmdbuf.data());
        vkDestroyCommandPool(_dev, _cmdpool, nullptr);

        for (const auto iter : _swpchain_imgv) {
            vkDestroyImageView(_dev, iter, nullptr);
        }
        vkDestroySwapchainKHR(_dev, _swpchain, nullptr);
        vkDestroyDevice(_dev, nullptr);
        vkDestroySurfaceKHR(_inst, _surf, nullptr);
        vkDestroyInstance(_inst, nullptr);
        glfwDestroyWindow(_glfw);
        glfwTerminate();
    }

    KanVul() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        _glfw = glfwCreateWindow(800, 800, "KanVul_clearcolorimage", nullptr, nullptr);
        glfwShowWindow(_glfw);

        InitInstance();
        glfwCreateWindowSurface(_inst, _glfw, nullptr, &_surf);
        InitPhysicalDevice();
        InitDevice();
        InitSwapchain();
        InitCmdPool();
        InitCmdBuffers();
    }

    void Run() {
        /* set up source buffer for copy */
        VkBuffer srcbuf;
        VkBufferCreateInfo bufCreateInfo {};
        bufCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufCreateInfo.pNext = nullptr;
        bufCreateInfo.flags = 0;
        bufCreateInfo.size = 16*16*4;
        bufCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufCreateInfo.queueFamilyIndexCount = 0;
        bufCreateInfo.pQueueFamilyIndices = nullptr;
        vkCreateBuffer(_dev, &bufCreateInfo, nullptr, &srcbuf);

        VkMemoryRequirements memreq {};
        vkGetBufferMemoryRequirements(_dev, srcbuf, &memreq);

        VkMemoryAllocateInfo memAllocInfo {};
        memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAllocInfo.pNext = nullptr;
        memAllocInfo.allocationSize = memreq.size;
        memAllocInfo.memoryTypeIndex = 0;
        VkDeviceMemory srcmem;
        vkAllocateMemory(_dev, &memAllocInfo, nullptr, &srcmem);

        uint8_t *pSRC = nullptr;
        vkMapMemory(_dev, srcmem, 0, memreq.size, 0, (void **)&pSRC);
        for (int32_t i = 0; i < memreq.size / 4; i++) {
            *(pSRC + 4*i + 2) = i;
        }
        vkUnmapMemory(_dev, srcmem);
        vkBindBufferMemory(_dev, srcbuf, srcmem, 0);

        /* record begin command buffer */
        VkCommandBufferBeginInfo cbi {};
        cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbi.pNext = nullptr;
        cbi.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        cbi.pInheritanceInfo = nullptr;

        VkClearColorValue color = {1.0, 1.0, 0.0, 1.0};
        VkImageSubresourceRange rg {};
        rg.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        rg.baseMipLevel = 0;
        rg.levelCount = 1;
        rg.baseArrayLayer = 0;
        rg.layerCount = 1;

        for (uint8_t i = 0; i < _cmdbuf.size(); i++) {
            vkBeginCommandBuffer(_cmdbuf[i], &cbi);

            /* image layout transition: undefined --> transfer dst */
            VkImageMemoryBarrier imgbarrier {};
            imgbarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imgbarrier.pNext = nullptr;
            imgbarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            imgbarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imgbarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imgbarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imgbarrier.srcQueueFamilyIndex = 0;
            imgbarrier.dstQueueFamilyIndex = 0;
            imgbarrier.image = _swpchain_img[i];
            imgbarrier.subresourceRange = rg;
            vkCmdPipelineBarrier(_cmdbuf[i], VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imgbarrier);

            vkCmdClearColorImage(_cmdbuf[i], _swpchain_img[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &rg);

            VkBufferImageCopy region[16];
            for (uint8_t k = 0; k < 16; k++) {
                region[k].bufferOffset = 0;
                region[k].bufferRowLength = 16;
                region[k].bufferImageHeight = 16;
                region[k].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region[k].imageSubresource.mipLevel = 0;
                region[k].imageSubresource.baseArrayLayer = 0;
                region[k].imageSubresource.layerCount = 1;
                region[k].imageOffset = VkOffset3D{k*16, k*16, k*16};
                region[k].imageExtent = VkExtent3D{16, 16, 1};
            }
            vkCmdCopyBufferToImage(_cmdbuf[i], srcbuf, _swpchain_img[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 16, region);

            /* image layout transition: transfer dst --> present */
            VkImageMemoryBarrier presentbarrier {};
            presentbarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            presentbarrier.pNext = nullptr;
            presentbarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            presentbarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            presentbarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            presentbarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            presentbarrier.srcQueueFamilyIndex = 0;
            presentbarrier.dstQueueFamilyIndex = 0;
            presentbarrier.image = _swpchain_img[i];
            presentbarrier.subresourceRange = rg;
            vkCmdPipelineBarrier(_cmdbuf[i], VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &presentbarrier);

            vkEndCommandBuffer(_cmdbuf[i]);
        }

        VkSemaphoreCreateInfo swpImgAcquireSemaInfo {};
        swpImgAcquireSemaInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        vkCreateSemaphore(_dev, &swpImgAcquireSemaInfo, nullptr, &_swpImgAcquire);

        uint32_t ImageIndex = 0;
        while (!glfwWindowShouldClose(_glfw)) {
            glfwPollEvents();

            vkAcquireNextImageKHR(_dev, _swpchain, UINT64_MAX, _swpImgAcquire, VK_NULL_HANDLE, &ImageIndex);

            VkSubmitInfo si {};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.pNext = nullptr;
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores = &_swpImgAcquire;
            VkPipelineStageFlags wm = VK_PIPELINE_STAGE_TRANSFER_BIT;
            si.pWaitDstStageMask = &wm;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &_cmdbuf[ImageIndex];
            si.signalSemaphoreCount = 0;
            si.pSignalSemaphores = nullptr;

            vkQueueSubmit(_queue, 1, &si, VK_NULL_HANDLE);

            VkPresentInfoKHR pi {};
            pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            pi.pNext = nullptr;
            pi.waitSemaphoreCount = 0;
            pi.pWaitSemaphores = nullptr;
            pi.swapchainCount = 1;
            pi.pSwapchains = &_swpchain;
            pi.pImageIndices = &ImageIndex;
            pi.pResults = nullptr;

            vkQueuePresentKHR(_queue, &pi);
        }

        /* clean up */
        vkQueueWaitIdle(_queue);
        vkFreeMemory(_dev, srcmem, nullptr);
        vkDestroyBuffer(_dev, srcbuf, nullptr);
    }

    void InitInstance() {
        VkApplicationInfo ai {};
        ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pApplicationName = "KanVul_copybuffertoimage";
        ai.pEngineName = "KanVul_copybuffertoimage";
        ai.apiVersion = VK_API_VERSION_1_1;

        VkInstanceCreateInfo ii {};
        ii.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ii.pApplicationInfo = &ai;

        vector<const char *> ie;
        ie.push_back("VK_KHR_surface");
        ie.push_back("VK_KHR_xcb_surface");
        ii.enabledExtensionCount = ie.size();
        ii.ppEnabledExtensionNames = ie.data();

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
        di.queueCreateInfoCount = 1;
        di.pQueueCreateInfos = &qi;

        vector<const char *> de;
        de.push_back("VK_KHR_swapchain");
        di.enabledExtensionCount = de.size();
        di.ppEnabledExtensionNames = de.data();

        vkCreateDevice(_pdev[0], &di, nullptr, &_dev);
        VkBool32 presentSupported = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(_pdev[0], 0, _surf, &presentSupported);
        assert(VK_TRUE == presentSupported);
    }

    void InitSwapchain() {
        uint32_t fmtc = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(_pdev[0], _surf, &fmtc, nullptr);
        std::vector<VkSurfaceFormatKHR> fmt;
        fmt.resize(fmtc);
        vkGetPhysicalDeviceSurfaceFormatsKHR(_pdev[0], _surf, &fmtc, fmt.data());

        VkSurfaceCapabilitiesKHR cap;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_pdev[0], _surf, &cap);

        VkSwapchainCreateInfoKHR sci;
        sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        sci.pNext = nullptr;
        sci.flags = 0;
        sci.surface = _surf;
        sci.minImageCount = cap.minImageCount;
        sci.imageFormat = fmt[0].format;
        sci.imageColorSpace = fmt[0].colorSpace;
        sci.imageExtent = cap.currentExtent;
        sci.imageArrayLayers = 1;
        sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sci.queueFamilyIndexCount = 0;
        sci.pQueueFamilyIndices = nullptr;
        sci.preTransform = cap.currentTransform;
        sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        sci.clipped = VK_TRUE;
        sci.oldSwapchain = VK_NULL_HANDLE;

        vkCreateSwapchainKHR(_dev, &sci, nullptr, &_swpchain);

        uint32_t scimgc = 0;
        vkGetSwapchainImagesKHR(_dev, _swpchain, &scimgc, nullptr);
        _swpchain_img.resize(scimgc);
        _swpchain_imgv.resize(scimgc);
        vkGetSwapchainImagesKHR(_dev, _swpchain, &scimgc, _swpchain_img.data());

        for (uint32_t i = 0; i < scimgc; i++) {
            VkImageViewCreateInfo imgvi;
            imgvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imgvi.pNext = nullptr;
            imgvi.flags = 0;
            imgvi.image = _swpchain_img[i];
            imgvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imgvi.format = fmt[0].format;
            imgvi.components.r = VK_COMPONENT_SWIZZLE_R;
            imgvi.components.g = VK_COMPONENT_SWIZZLE_G;
            imgvi.components.b = VK_COMPONENT_SWIZZLE_B;
            imgvi.components.a = VK_COMPONENT_SWIZZLE_A;
            imgvi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imgvi.subresourceRange.baseMipLevel = 0;
            imgvi.subresourceRange.levelCount = 1;
            imgvi.subresourceRange.baseArrayLayer = 0;
            imgvi.subresourceRange.layerCount = 1;

            vkCreateImageView(_dev, &imgvi, nullptr, &_swpchain_imgv[i]);
        }
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
        /* clear screen command buffer */
        VkCommandBufferAllocateInfo bi {};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        bi.pNext = nullptr;
        bi.commandPool = _cmdpool;
        bi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        bi.commandBufferCount = _swpchain_img.size();

        _cmdbuf.resize(_swpchain_img.size());
        vkAllocateCommandBuffers(_dev, &bi, _cmdbuf.data());
    }

private:
    GLFWwindow *_glfw;
    VkInstance _inst;
    VkSurfaceKHR _surf;
    vector<VkPhysicalDevice> _pdev;
    VkDevice _dev;
    VkSwapchainKHR _swpchain;
    vector<VkImage> _swpchain_img;
    vector<VkImageView> _swpchain_imgv;
    VkCommandPool _cmdpool;
    vector<VkCommandBuffer> _cmdbuf;
    VkQueue _queue;
    VkSemaphore _swpImgAcquire;
};

int main(int argc, char const *argv[])
{
    KanVul app;
    app.Run();

    return 0;
}

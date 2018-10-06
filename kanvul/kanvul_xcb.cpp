#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>
#include <xcb/xcb.h>
#include <vector>
#include <cassert>
#include <cstdlib>

using std::vector;

class KanVul {
public:
    ~KanVul() {
        vkQueueWaitIdle(_queue);

        vkDestroySemaphore(_dev, _swpImgAcquire, nullptr);
        vkDestroySemaphore(_dev, _renderImgFinished, nullptr);
        vkFreeCommandBuffers(_dev, _cmdpool, _cmdbuf.size(), _cmdbuf.data());
        vkDestroyCommandPool(_dev, _cmdpool, nullptr);

        for (const auto iter : _swpchain_imgv) {
            vkDestroyImageView(_dev, iter, nullptr);
        }
        vkDestroySwapchainKHR(_dev, _swpchain, nullptr);
        vkDestroyDevice(_dev, nullptr);
        vkDestroySurfaceKHR(_inst, _surf, nullptr);
        vkDestroyInstance(_inst, nullptr);

        xcb_destroy_window(_xcb_conn, _xcb_wid);
        xcb_disconnect(_xcb_conn);
    }

    KanVul() {
        InitXCB();
        InitInstance();
        InitXCBSurface();
        InitPhysicalDevice();
        InitDevice();
        InitSwapchain();
        InitCmdPool();
        InitCmdBuffers();
        InitSyncObj();
    }

    void Run() {
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

        uint32_t ImageIndex = 0;
        xcb_generic_event_t *event;
        while (!_quit) {
            vkAcquireNextImageKHR(_dev, _swpchain, UINT64_MAX, _swpImgAcquire, VK_NULL_HANDLE, &ImageIndex);

            VkSubmitInfo si {};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            /* stages prior to output color stage can already process while output color stage must
             * wait until _swpImgAcquire semaphore is signaled.
             */
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores = &_swpImgAcquire;
            VkPipelineStageFlags wm = VK_PIPELINE_STAGE_TRANSFER_BIT;
            si.pWaitDstStageMask = &wm;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &_cmdbuf[ImageIndex];
            /* signal finish of render process, status from unsignaled to signaled */
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores = &_renderImgFinished;

            vkQueueSubmit(_queue, 1, &si, VK_NULL_HANDLE);

            VkPresentInfoKHR pi {};
            pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            pi.pNext = nullptr;
            /* presentation engine can only start present image until the render executions onto the
             * image have been finished (_renderImgFinished semaphore signaled)
             */
            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = &_renderImgFinished;
            pi.swapchainCount = 1;
            pi.pSwapchains = &_swpchain;
            pi.pImageIndices = &ImageIndex;
            pi.pResults = nullptr;

            vkQueuePresentKHR(_queue, &pi);

            while ((event = xcb_poll_for_event(_xcb_conn))) {
                switch (event->response_type & 0x7f) {
                case XCB_CLIENT_MESSAGE:
                    if ((*(xcb_client_message_event_t*)event).data.data32[0] == (*_atom_wm_delete_window).atom) {
                        _quit = true;
                    }
                    break;
                default:
                    break;
                }
                free(event);
            }
        }
    }

    void InitXCB() {
        int screennum, err;
        _xcb_conn = xcb_connect(nullptr, &screennum);
        err = xcb_connection_has_error(_xcb_conn);
        assert(err == 0);

        const xcb_setup_t *setup = xcb_get_setup(_xcb_conn);
        xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);

        while (screennum-- > 0) {
            xcb_screen_next(&iter);
        }

        xcb_screen_t *screen = iter.data;

        _xcb_wid = xcb_generate_id(_xcb_conn);

        uint32_t value_mask = XCB_CW_EVENT_MASK;
        uint32_t value_list = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_EXPOSURE;
        xcb_create_window(_xcb_conn, XCB_COPY_FROM_PARENT, _xcb_wid, screen->root, 0, 0,
            800, 800, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
            value_mask, &value_list);

        /* Magic code that will send notification when window is destroyed */
        xcb_intern_atom_cookie_t cookie = xcb_intern_atom(_xcb_conn, 1, 12, "WM_PROTOCOLS");
        xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(_xcb_conn, cookie, 0);

        xcb_intern_atom_cookie_t cookie2 = xcb_intern_atom(_xcb_conn, 0, 16, "WM_DELETE_WINDOW");
        reply = xcb_intern_atom_reply(_xcb_conn, cookie2, 0);

        xcb_change_property(_xcb_conn, XCB_PROP_MODE_REPLACE, _xcb_wid, (*reply).atom, 4, 32, 1, &(*reply).atom);
        free(reply);

        xcb_map_window(_xcb_conn, _xcb_wid);

        uint32_t loc[2] = { 0, 0 };
        xcb_configure_window(_xcb_conn, _xcb_wid, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, loc);

        xcb_flush(_xcb_conn);

        xcb_generic_event_t *e;
        while ((e = xcb_wait_for_event(_xcb_conn))) {
            if ((e->response_type & ~0x80) == XCB_EXPOSE)
                break;
        }

        _quit = false;
    }

    void InitInstance() {
        VkApplicationInfo ai {};
        ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
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

    void InitXCBSurface() {
        VkXcbSurfaceCreateInfoKHR xcbSurfaceInfo {};
        xcbSurfaceInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        xcbSurfaceInfo.connection = _xcb_conn;
        xcbSurfaceInfo.window = _xcb_wid;
        vkCreateXcbSurfaceKHR(_inst, &xcbSurfaceInfo, nullptr, &_surf);
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
        di.pEnabledFeatures = nullptr;

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

    void InitSyncObj() {
        VkSemaphoreCreateInfo semaInfo {};
        semaInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore(_dev, &semaInfo, nullptr, &_swpImgAcquire);
        vkCreateSemaphore(_dev, &semaInfo, nullptr, &_renderImgFinished);
    }

private:
    xcb_connection_t *_xcb_conn;
    xcb_window_t _xcb_wid;
    xcb_intern_atom_reply_t *_atom_wm_delete_window;
    bool _quit;
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
    /* sync between presentation engine and start of command execution */
    VkSemaphore _swpImgAcquire;
    /* sync between finish of command execution and present request to presentation engine */
    VkSemaphore _renderImgFinished;
};

int main(int argc, char const *argv[])
{
    KanVul app;
    app.Run();

    return 0;
}

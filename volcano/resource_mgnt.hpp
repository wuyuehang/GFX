#ifndef _RESOURCE_MGNT_HPP
#define _RESOURCE_MGNT_HPP

#include <vulkan/vulkan.h>
#include <cassert>
#include <string>
#include <map>

using std::pair;
using std::make_pair;
using std::string;
using std::map;

class ResouceMgnt {
public:
    ~ResouceMgnt() { _buf.clear(); }
    ResouceMgnt() { _buf.clear(); }

    /* Find a memory in `memoryTypeBitsRequirement` that includes all of `requiredProperties`
     * this function is copied from vulkan spec */
    uint32_t findProperties(const VkPhysicalDeviceMemoryProperties* pMemoryProperties,
        uint32_t memoryTypeBitsRequirement, VkMemoryPropertyFlags requiredProperties) {
        const uint32_t memoryCount = pMemoryProperties->memoryTypeCount;

        for (uint32_t idx = 0; idx < memoryCount; ++idx) {
            const uint32_t memoryTypeBits = (1 << idx);
            const bool isRequiredMemoryType = memoryTypeBitsRequirement & memoryTypeBits;

            const VkMemoryPropertyFlags properties = pMemoryProperties->memoryTypes[idx].propertyFlags;
            const bool hasRequiredProperties = (properties & requiredProperties) == requiredProperties;

            if (isRequiredMemoryType && hasRequiredProperties)
                return idx;
        }
        assert(0);
    }

    VkBuffer allocBuf(VkDevice dev, VkPhysicalDeviceMemoryProperties pdmp,
            VkBufferUsageFlags usage, VkDeviceSize size, const void *pDATA,
            const string& token, VkMemoryPropertyFlags requiredProperties = 0) {

        VkBuffer buf = VK_NULL_HANDLE;
        VkDeviceMemory bufMem = VK_NULL_HANDLE;

        VkBufferCreateInfo bufInfo {};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = size;
        bufInfo.usage = usage;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(dev, &bufInfo, nullptr, &buf);

        VkMemoryRequirements req {};
        vkGetBufferMemoryRequirements(dev, buf, &req);

        int32_t memoryType = findProperties(&pdmp, req.memoryTypeBits, requiredProperties);

        VkMemoryAllocateInfo allocBufInfo {};
        allocBufInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocBufInfo.allocationSize = req.size;
        allocBufInfo.memoryTypeIndex = memoryType;

        vkAllocateMemory(dev, &allocBufInfo, nullptr, &bufMem);
        vkBindBufferMemory(dev, buf, bufMem, 0);

        if (pDATA != nullptr) {
            uint8_t *pDST = nullptr;
            vkMapMemory(dev, bufMem, 0, req.size, 0, (void **)&pDST);

            uint8_t *pSRC = (uint8_t *)pDATA;
            for (uint32_t i = 0; i < req.size; i++) {
                *(pDST + i) = *(pSRC + i);
            }

            vkUnmapMemory(dev, bufMem);
        }

        _buf.insert(map<const string, pair<VkBuffer, VkDeviceMemory>>::value_type(token, make_pair(buf, bufMem)));

        return buf;
    }

    void freeBuf(VkDevice dev) {
        for (const auto & iter : _buf) {
            vkFreeMemory(dev, iter.second.second, nullptr);
            vkDestroyBuffer(dev, iter.second.first, nullptr);
        }
    }

    VkBuffer queryBuf(const string token) {
        auto res = _buf.find(token);
        assert(res != _buf.end());
        return res->second.first;
    }

    void updateBufContent(VkDevice dev, const string token, const void *pDATA) {
        auto res = _buf.find(token);
        assert(res != _buf.end());
        VkBuffer buf = res->second.first;
        VkDeviceMemory bufMem = res->second.second;

        VkMemoryRequirements req {};
        vkGetBufferMemoryRequirements(dev, buf, &req);

        uint8_t *pDST = nullptr;
        vkMapMemory(dev, bufMem, 0, req.size, 0, (void **)&pDST);

        uint8_t *pSRC = (uint8_t *)pDATA;
        for (uint32_t i = 0; i < req.size; i++) {
            *(pDST + i) = *(pSRC + i);
        }

        vkUnmapMemory(dev, bufMem);
    }

private:
    map<const string, pair<VkBuffer, VkDeviceMemory>> _buf;
};

#endif

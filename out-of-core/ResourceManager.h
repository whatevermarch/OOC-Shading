#pragma once

#include "VkUtils.h"

#include <vk_mem_alloc.h>


typedef struct Buffer {
	VkBuffer buffer;
	VkDeviceMemory memory;
	VmaAllocation allocation;
} Buffer;

typedef struct Texture {
	VkSampler sampler;
	VkImage image;
	VkImageLayout lastImgLayout;
	VkDeviceMemory memory;
	VmaAllocation allocation;
	VkImageView view;
	uint32_t width, height;
	bool isInGPU;
} Texture;

class ResourceManager
{
public:
	ResourceManager(VkPhysicalDevice physicalDevice, VkDevice device);
	virtual ~ResourceManager();

	void createBuffer(VmaMemoryUsage memUsage, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer * buffer, VmaAllocation * allocation, void **pPersistentlyMappedData);
	void createImage(VmaMemoryUsage memUsage, uint32_t width, uint32_t height, VkImageUsageFlags usage, VkImage *image, VmaAllocation *allocation, void **pPersistentlyMappedData);

	void mapMemory(VmaAllocation allocation, void** ppData);
	void unmapMemory(VmaAllocation allocation);

	void destroyBuffer(VkBuffer buffer, VmaAllocation allocation);
	void destroyImage(VkImage image, VmaAllocation allocation);

	void transitionImageLayout(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);

	void migrateTexture(const VkCommandBuffer& cmdBuffer, const Texture& srcTexture, Texture* dstTexture);

private:
	VmaAllocator allocator;

	VkPhysicalDevice physicalDevice;
};


#pragma once

#include "VkUtils.h"

#include <vk_mem_alloc.h>

#include <queue>
#include <stack>


#define PSUEDO_DEVICE_LIMIT 200 // in MBs

struct Resource {
	VmaAllocation allocation;

	virtual void updateDescriptorInfo() = 0;
};

typedef struct Buffer : Resource {
	VkBuffer buffer;
	VkDeviceSize size;
	bool isInGPU;

	VkDescriptorBufferInfo descInfo;
	inline virtual void updateDescriptorInfo() {
		descInfo.buffer = buffer;
		descInfo.offset = 0;
		descInfo.range = size;
	}
} Buffer;

typedef struct Image : Resource {
	VkSampler sampler;
	VkImage image;
	VkImageLayout lastImgLayout;
	VkImageView view;
	uint32_t width, height;
	VkFormat format;
	bool isInGPU;

	VkDescriptorImageInfo descInfo;
	inline virtual void updateDescriptorInfo() {
		descInfo.imageLayout = lastImgLayout;
		descInfo.imageView = view;
		descInfo.sampler = sampler;
	};
} Image;

class ResourceManager
{
public:
	ResourceManager(VkPhysicalDevice physicalDevice, VkDevice device, VkCommandPool cmdPool, VkQueue cmdQueue);
	virtual ~ResourceManager();

	void createBuffer(VmaMemoryUsage memUsage, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer * buffer, VmaAllocation * allocation, void **pPersistentlyMappedData);
	void createBufferInDevice(VkDeviceSize size, VkBufferUsageFlags usage, Buffer *buffer, void* pData);
	void createBufferInHost(VkDeviceSize size, VkBufferUsageFlags usage, Buffer *buffer, void* pData);

	void createImage(VmaMemoryUsage memUsage, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImage *image, VmaAllocation *allocation, void **pPersistentlyMappedData);
	void createImageInDevice(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, Image *image, void* pData);
	void createImageInHost(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, Image *image, void* pData);

	void mapMemory(VmaAllocation allocation, void** ppData);
	void unmapMemory(VmaAllocation allocation);

	void destroyBuffer(VkBuffer buffer, VmaAllocation allocation);
	void destroyImage(VkImage image, VmaAllocation allocation);

	void transitionImageLayout(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);

	void migrateTexture(Image& texture);

private:
	VmaAllocator allocator;

	VkPhysicalDevice physicalDevice;

	VkDevice device;

	VkCommandPool cmdPool;
	VkCommandBuffer cmdBuffer;
	VkQueue cmdQueue;

	// my work
	// Heap<Resource> deviceHeap // may use priority queue (#include <queue>) , (default) less is drown (last to pop)
	// Stack<Resource> hostStack // use stack (#include <stack>)

	struct ResourceCompare {
		bool operator() (const Resource* lhs, const Resource* rhs) {
			return lhs->allocation->GetSize() > rhs->allocation->GetSize();
		}
	};
	std::priority_queue<Resource*, std::vector<Resource*>, ResourceCompare> deviceHeap;

	std::stack<Resource*> hostStack;

	uint32_t totalDeviceUsage, totalHostUsage;

	bool isAvailable();

	void createCmdBuffer();
	void beginCmdBuffer();
	void flushCmdBuffer();
};


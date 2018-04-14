#pragma once

#include "VkUtils.h"

#include <vk_mem_alloc.h>

#include <vector>
#include <queue>
#include <algorithm>


#define PSUEDO_DEVICE_LIMIT 10 // in MBs

enum ResourceType {
	RESOURCE_TYPE_BUFFER = 0,
	RESOURCE_TYPE_IMAGE = 1
};

struct Resource {
	VmaAllocation allocation;
	ResourceType type;
	bool isInGPU;
	bool isMigratable;
	bool isBoundToDesc;

	virtual void updateDescriptorInfo() = 0;
};

struct gtResource {
	bool operator() (Resource * const &lhs, Resource * const &rhs);
};

class ResourceHeap : public std::priority_queue<Resource*, std::vector<Resource*>, gtResource>
{
public:
	bool remove(const Resource* value);
	void checkMember();
};

struct Buffer : Resource {
	VkBuffer buffer;
	VkDeviceSize size;
	VkBufferUsageFlags usage;

	inline Buffer() {
		type = RESOURCE_TYPE_BUFFER;
		isBoundToDesc = false;
	}

	VkDescriptorBufferInfo descInfo;
	inline virtual void updateDescriptorInfo() {
		descInfo.buffer = buffer;
		descInfo.offset = 0;
		descInfo.range = size;
	}
};

struct Image : Resource {
	VkSampler sampler;
	VkImage image;
	VkImageLayout lastImgLayout;
	VkImageView view;
	uint32_t width, height;
	VkFormat format;
	VkImageUsageFlags usage;

	inline Image() {
		type = RESOURCE_TYPE_IMAGE;
		isBoundToDesc = false;
	}

	VkDescriptorImageInfo descInfo;
	inline virtual void updateDescriptorInfo() {
		descInfo.imageLayout = lastImgLayout;
		descInfo.imageView = view;
		descInfo.sampler = sampler;
	};
};

class ResourceManager
{
public:
	ResourceManager(VkPhysicalDevice physicalDevice, VkDevice device, VkCommandPool cmdPool, VkQueue cmdQueue);
	virtual ~ResourceManager();

	void createBuffer(VmaMemoryUsage memUsage, VkDeviceSize size, VkBufferUsageFlags usage, Buffer *buffer, void **pPersistentlyMappedData);
	void createBufferInDevice(VkDeviceSize size, VkBufferUsageFlags usage, Buffer *buffer, void* pData);
	void createBufferInHost(VkDeviceSize size, VkBufferUsageFlags usage, Buffer *buffer, void* pData);

	void createImage(VmaMemoryUsage memUsage, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, Image *image, void **pPersistentlyMappedData);
	void createImageInDevice(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, Image *image, void* pData);
	void createImageInHost(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, Image *image, void* pData);

	void mapMemory(VmaAllocation allocation, void** ppData);
	void unmapMemory(VmaAllocation allocation);

	void destroyBuffer(Buffer &buffer);
	void destroyImage(Image &image);

	void createImageView(VkImage image, VkFormat format, VkImageView* imageView);
	void transitionImageLayout(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);

	void migrateTexture(Image& texture);
	void migrateBuffer(Buffer& buffer);
	void migrateResource(Resource& resource);

	// my work
	void reduceMemoryBound(VkDeviceSize amount);
	void extendMemoryBound(VkDeviceSize amount);

	//test
	void printHeap();
	inline size_t getDeviceHeapSize() { return deviceHeap.size(); }
	inline size_t getHostHeapSize() { return hostHeap.size(); }
	void inspectHeap();

private:
	VmaAllocator allocator;

	VkPhysicalDevice physicalDevice;
	VkPhysicalDeviceMemoryProperties deviceMemoryProperties;

	VkDevice device;

	VkCommandPool cmdPool;
	VkCommandBuffer cmdBuffer;
	VkQueue cmdQueue;

	// my work
	ResourceHeap deviceHeap, hostHeap;
	VkDeviceSize totalDeviceUsage = 0, totalHostUsage = 0, pseudoDeviceLimit = PSUEDO_DEVICE_LIMIT * 1000000;

	bool checkAndMoveToGPU(VkMemoryRequirements& memReqs, VmaMemoryUsage& memUsage);
	VkDeviceSize getRequiredImageSize(VkImageCreateInfo* info);

	void createCmdBuffer();
	void beginCmdBuffer();
	void flushCmdBuffer();
};


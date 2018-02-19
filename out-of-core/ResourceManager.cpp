#define VMA_IMPLEMENTATION

#include "ResourceManager.h"


ResourceManager::ResourceManager(VkPhysicalDevice physicalDevice, VkDevice device)
{
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = physicalDevice;
	allocatorInfo.device = device;

	VK_CHECK_RESULT(vmaCreateAllocator(&allocatorInfo, &allocator));

	this->physicalDevice = physicalDevice;

}


ResourceManager::~ResourceManager()
{
	vmaDestroyAllocator(allocator);
}

void ResourceManager::createBuffer(VmaMemoryUsage memUsage, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer * buffer, VmaAllocation * allocation, void **pPersistentlyMappedData)
{
	VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	//bool isMappable = (memUsage == VMA_MEMORY_USAGE_CPU_ONLY) || (memUsage == VMA_MEMORY_USAGE_CPU_TO_GPU);

	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = memUsage;

	// if we want persistent mapped pointer
	if (pPersistentlyMappedData != nullptr)
		allocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo allocInfo = {};

	VK_CHECK_RESULT(vmaCreateBuffer(allocator, &bufferInfo, &allocCreateInfo, buffer, allocation, &allocInfo));

	//if (isMappable) {
		//if (allocInfo.pMappedData == nullptr) 
			//throw std::runtime_error("Resource manager failed to map memory!");
	if(pPersistentlyMappedData != nullptr)
		*pPersistentlyMappedData = allocInfo.pMappedData;
	//}
		
}

void ResourceManager::createImage(VmaMemoryUsage memUsage, uint32_t width, uint32_t height, VkImageUsageFlags usage, VkImage *image, VmaAllocation *allocation, void **pPersistentlyMappedData)
{
	VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = usage;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (memUsage == VMA_MEMORY_USAGE_GPU_ONLY)
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	else
		imageInfo.tiling = VK_IMAGE_TILING_LINEAR;

	//bool isMappable = (memUsage == VMA_MEMORY_USAGE_CPU_ONLY) || (memUsage == VMA_MEMORY_USAGE_CPU_TO_GPU);

	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = memUsage;

	if (pPersistentlyMappedData != nullptr)
		allocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo allocInfo = {};

	VK_CHECK_RESULT(vmaCreateImage(allocator, &imageInfo, &allocCreateInfo, image, allocation, &allocInfo));

	if (pPersistentlyMappedData != nullptr)
		*pPersistentlyMappedData = allocInfo.pMappedData;

}

void ResourceManager::mapMemory(VmaAllocation allocation, void ** ppData)
{
	VK_CHECK_RESULT(vmaMapMemory(allocator,allocation,ppData));
}

void ResourceManager::unmapMemory(VmaAllocation allocation)
{
	vmaUnmapMemory(allocator, allocation);
}

void ResourceManager::destroyBuffer(VkBuffer buffer, VmaAllocation allocation)
{
	vmaDestroyBuffer(allocator, buffer, allocation);
}

void ResourceManager::destroyImage(VkImage image, VmaAllocation allocation)
{
	vmaDestroyImage(allocator, image, allocation);
}

void ResourceManager::transitionImageLayout(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
{
	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	VkPipelineStageFlags sourceStage;
	VkPipelineStageFlags destinationStage;

	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else {
		throw std::invalid_argument("unsupported layout transition!");
	}

	vkCmdPipelineBarrier(
		cmdBuffer,
		sourceStage, destinationStage,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier
	);
}

void ResourceManager::migrateTexture(const VkCommandBuffer& cmdBuffer, const Texture& srcTexture, Texture* dstTexture)
{
	if( dstTexture == nullptr)
		throw std::runtime_error("Destination texture param does not exist.");

	VmaMemoryUsage memUsage;
	if (srcTexture.isInGPU)
		memUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
	else
		memUsage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProperties);

	if (!(formatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
		throw std::runtime_error("Secondary Image doesn't support desired format.");

	createImage(
		memUsage, 
		srcTexture.width, 
		srcTexture.height, 
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 
		&dstTexture->image, 
		&dstTexture->allocation,
		nullptr
	);

	transitionImageLayout(cmdBuffer, srcTexture.image, srcTexture.lastImgLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	transitionImageLayout(cmdBuffer, dstTexture->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkImageCopy copyRegionI = {};
	copyRegionI.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegionI.srcSubresource.mipLevel = 0;
	copyRegionI.srcSubresource.baseArrayLayer = 0;
	copyRegionI.srcSubresource.layerCount = 1;
	copyRegionI.srcOffset = { 0, 0, 0 };
	copyRegionI.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegionI.dstSubresource.mipLevel = 0;
	copyRegionI.dstSubresource.baseArrayLayer = 0;
	copyRegionI.dstSubresource.layerCount = 1;
	copyRegionI.dstOffset = { 0, 0, 0 };
	copyRegionI.extent = {
		srcTexture.width,
		srcTexture.height,
		1
	};

	vkCmdCopyImage(
		cmdBuffer, 
		srcTexture.image, 
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		dstTexture->image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, 
		&copyRegionI
	);

	transitionImageLayout(cmdBuffer, dstTexture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	dstTexture->lastImgLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	dstTexture->width = srcTexture.width;
	dstTexture->height = srcTexture.height;
	dstTexture->isInGPU = true;
	/*
	destroyImage(srcTexture.image, srcTexture.allocation);

	srcTexture.image = dstImage;
	srcTexture.allocation = dstAlloc;
	srcTexture.isInGPU = false;
	*/
}

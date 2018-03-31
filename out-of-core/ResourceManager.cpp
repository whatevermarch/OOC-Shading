#define VMA_IMPLEMENTATION

#include "ResourceManager.h"

#include <limits.h>


ResourceManager::ResourceManager(VkPhysicalDevice physicalDevice, VkDevice device, VkCommandPool cmdPool, VkQueue cmdQueue)
{
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = physicalDevice;
	allocatorInfo.device = device;

	VK_CHECK_RESULT(vmaCreateAllocator(&allocatorInfo, &allocator));

	this->physicalDevice = physicalDevice;
	this->device = device;
	this->cmdPool = cmdPool;
	this->cmdQueue = cmdQueue;

	createCmdBuffer();
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

	// test start
	VkBuffer pseudoBuffer;
	VkMemoryRequirements memReqs;
	VK_CHECK_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &pseudoBuffer));
	vkGetBufferMemoryRequirements(device, pseudoBuffer, &memReqs);
	vkDestroyBuffer(device, pseudoBuffer, nullptr);

	if (memUsage == VMA_MEMORY_USAGE_GPU_ONLY && totalDeviceUsage + memReqs.size > PSUEDO_DEVICE_LIMIT * 0.9) {
		std::vector<Resource*> URNotTheFace;
		Resource* pSmallestResource;
		uint32_t smallestResourceSize;
		uint32_t oldSpace = PSUEDO_DEVICE_LIMIT * 0.9 - totalDeviceUsage;
		uint32_t newSpace = oldSpace;

		// what if one loop and heap empty
		while (!deviceHeap.empty()) {
			pSmallestResource = deviceHeap.top();
			smallestResourceSize = pSmallestResource->allocation->GetSize();

			if (memReqs.size < smallestResourceSize) {
				for (Resource* pRes : URNotTheFace)
					deviceHeap.push(pRes);
				
				bufferInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
				break;
			}
			else if (newSpace >= memReqs.size){
				if (newSpace - memReqs.size < oldSpace) {
					// return resources poped up to heap
					for (Resource* pRes : URNotTheFace) 
						deviceHeap.push(pRes);
					
					bufferInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
					break;
				}
				else {
					// migrate those URNotTheFace resources
				}
			}

			URNotTheFace.push_back(pSmallestResource);
			deviceHeap.pop();

			newSpace += smallestResourceSize;
			totalDeviceUsage -= smallestResourceSize;
		}
	}

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

void ResourceManager::createBufferInDevice(VkDeviceSize size, VkBufferUsageFlags usage, Buffer *buffer, void * pData)
{
	if (pData == nullptr)
		throw std::invalid_argument("f(x):createBufferInDevice needs data to initiate.");

	Buffer stagingBuffer;

	void* pMappedData;

	createBuffer(
		VMA_MEMORY_USAGE_CPU_ONLY,
		size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		&stagingBuffer.buffer,
		&stagingBuffer.allocation,
		&pMappedData
	);

	memcpy(pMappedData, pData, size);

	createBuffer(
		VMA_MEMORY_USAGE_GPU_ONLY,
		size,
		usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		&buffer->buffer,
		&buffer->allocation,
		nullptr
	);

	beginCmdBuffer();

		VkBufferCopy copyRegionB = {};
		copyRegionB.size = size;
		vkCmdCopyBuffer(cmdBuffer, stagingBuffer.buffer, buffer->buffer, 1, &copyRegionB);

	flushCmdBuffer();

	destroyBuffer(stagingBuffer.buffer, stagingBuffer.allocation);

	// Initialize member value
	buffer->size = size;
	buffer->isInGPU = true;
}

void ResourceManager::createBufferInHost(VkDeviceSize size, VkBufferUsageFlags usage, Buffer *buffer, void * pData)
{
	void* pMappedData;

	createBuffer(
		VMA_MEMORY_USAGE_CPU_TO_GPU,
		size,
		usage,
		&buffer->buffer,
		&buffer->allocation,
		(pData == nullptr) ? nullptr : &pMappedData
	);

	if(pData != nullptr)
		memcpy(pMappedData, pData, size);

	buffer->size = size;
	buffer->isInGPU = false;
}

void ResourceManager::createImage(VmaMemoryUsage memUsage, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImage *image, VmaAllocation *allocation, void **pPersistentlyMappedData)
{
	VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
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

void ResourceManager::createImageInDevice(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, Image *image, void * pData)
{
	if (pData == nullptr)
		throw std::invalid_argument("f(x):createBufferInDevice needs data to initiate.");

	Buffer stagingBuffer;

	void* pMappedData;
	VkDeviceSize imageSize = width * height * 4;

	createBuffer(
		VMA_MEMORY_USAGE_CPU_ONLY,
		imageSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		&stagingBuffer.buffer,
		&stagingBuffer.allocation,
		&pMappedData
	);

	memcpy(pMappedData, pData, imageSize);

	createImage(
		VMA_MEMORY_USAGE_GPU_ONLY,
		width,
		height,
		format,
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		&image->image,
		&image->allocation,
		nullptr
	);

	beginCmdBuffer();
	
	transitionImageLayout(cmdBuffer, image->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkBufferImageCopy copyRegionBI = {};
	copyRegionBI.bufferOffset = 0;
	copyRegionBI.bufferRowLength = 0;
	copyRegionBI.bufferImageHeight = 0;
	copyRegionBI.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegionBI.imageSubresource.mipLevel = 0;
	copyRegionBI.imageSubresource.baseArrayLayer = 0;
	copyRegionBI.imageSubresource.layerCount = 1;
	copyRegionBI.imageOffset = { 0, 0, 0 };
	copyRegionBI.imageExtent = {
		width,
		height,
		1
	};

	vkCmdCopyBufferToImage(cmdBuffer, stagingBuffer.buffer, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegionBI);

	transitionImageLayout(cmdBuffer, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	flushCmdBuffer();

	destroyBuffer(stagingBuffer.buffer, stagingBuffer.allocation);

	image->width = width;
	image->height = height;
	image->format = format;
	image->lastImgLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	image->isInGPU = true;
}

void ResourceManager::createImageInHost(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, Image *image, void * pData)
{
	void* pMappedData;
	VkDeviceSize imageSize = width * height * 4;

	createImage(
		VMA_MEMORY_USAGE_CPU_TO_GPU,
		width,
		height,
		format,
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		&image->image,
		&image->allocation,
		nullptr
	);

	if (pData != nullptr)
		memcpy(pMappedData, pData, imageSize);

	beginCmdBuffer();

	transitionImageLayout(cmdBuffer, image->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	flushCmdBuffer();

	image->width = width;
	image->height = height;
	image->format = format;
	image->lastImgLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	image->isInGPU = false;
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

void ResourceManager::migrateTexture(Image& texture)
{
	if (texture.image == NULL)
		throw std::runtime_error("Destination texture param does not exist.");

	VmaMemoryUsage memUsage;
	if (texture.isInGPU) {
		memUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		texture.isInGPU = false;

		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProperties);
		if (!(formatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
			throw std::runtime_error("Secondary Image doesn't support desired format.");
	}
	else {
		memUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		texture.isInGPU = true;
	}

	VkImage dstImage;
	VmaAllocation dstAlloc;

	createImage(
		memUsage,
		texture.width,
		texture.height,
		texture.format,
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		&dstImage,
		&dstAlloc,
		nullptr
	);

	beginCmdBuffer();

	transitionImageLayout(cmdBuffer, texture.image, texture.lastImgLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	transitionImageLayout(cmdBuffer, dstImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

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
		texture.width,
		texture.height,
		1
	};

	vkCmdCopyImage(
		cmdBuffer,
		texture.image,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		dstImage,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&copyRegionI
	);

	transitionImageLayout(cmdBuffer, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	flushCmdBuffer();

	destroyImage(texture.image, texture.allocation);

	texture.image = dstImage;
	texture.allocation = dstAlloc;
	texture.lastImgLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	
}

void ResourceManager::createCmdBuffer()
{
	VkCommandBufferAllocateInfo cmdBufAllocateInfo = {};
	cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdBufAllocateInfo.commandPool = cmdPool;
	cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmdBufAllocateInfo.commandBufferCount = 1;

	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &cmdBuffer));
}

void ResourceManager::beginCmdBuffer()
{
	VkCommandBufferBeginInfo cmdBufInfo = {};
	cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));
}

void ResourceManager::flushCmdBuffer()
{
	VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));

	vkQueueWaitIdle(cmdQueue);
	
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmdBuffer;

	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.flags = 0;
	VkFence fence;
	VK_CHECK_RESULT(vkCreateFence(device, &fenceCreateInfo, nullptr, &fence));

	VK_CHECK_RESULT(vkQueueSubmit(cmdQueue, 1, &submitInfo, fence));

	VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, ULONG_MAX));

	vkDestroyFence(device, fence, nullptr);

}

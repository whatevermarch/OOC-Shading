#define VMA_IMPLEMENTATION

#include "ResourceManager.h"

#include <limits.h>
#include <algorithm>


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

	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &deviceMemoryProperties);
	createCmdBuffer();

}


ResourceManager::~ResourceManager()
{
	vmaDestroyAllocator(allocator);
}

void ResourceManager::createBuffer(VmaMemoryUsage memUsage, VkDeviceSize size, VkBufferUsageFlags usage, Buffer *buffer, void **pPersistentlyMappedData)
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

	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = memUsage;

	// if we want persistent mapped pointer
	if (pPersistentlyMappedData != nullptr)
		allocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo allocInfo = {};

	VK_CHECK_RESULT(vmaCreateBuffer(allocator, &bufferInfo, &allocCreateInfo, &buffer->buffer, &buffer->allocation, &allocInfo));

	buffer->size = size;
	buffer->isMigratable = false;
	buffer->usage = usage;

	if (memUsage == VMA_MEMORY_USAGE_GPU_ONLY) {
		totalDeviceUsage += memReqs.size;
		//deviceHeap.push(buffer);
		buffer->isInGPU = true;
	}
	else {
		totalHostUsage += memReqs.size;
		//hostHeap.push(buffer);
		buffer->isInGPU = false;

		if (pPersistentlyMappedData != nullptr)
			*pPersistentlyMappedData = allocInfo.pMappedData;
	}
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
		&stagingBuffer,
		&pMappedData
	);

	memcpy(pMappedData, pData, size);

	createBuffer(
		VMA_MEMORY_USAGE_GPU_ONLY,
		size,
		usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		buffer,
		nullptr
	);

	beginCmdBuffer();

		VkBufferCopy copyRegionB = {};
		copyRegionB.size = size;
		vkCmdCopyBuffer(cmdBuffer, stagingBuffer.buffer, buffer->buffer, 1, &copyRegionB);

	flushCmdBuffer();

	destroyBuffer(stagingBuffer);
}

void ResourceManager::createBufferInHost(VkDeviceSize size, VkBufferUsageFlags usage, Buffer *buffer, void * pData)
{
	void* pMappedData;

	createBuffer(
		VMA_MEMORY_USAGE_CPU_TO_GPU,
		size,
		usage,
		buffer,
		(pData == nullptr) ? nullptr : &pMappedData
	);

	if(pData != nullptr)
		memcpy(pMappedData, pData, size);
}

void ResourceManager::createImage(VmaMemoryUsage memUsage, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, Image *image, void **pPersistentlyMappedData)
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

	if (usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT && usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		image->isMigratable = true;
	else
		image->isMigratable = false;

	VkDeviceSize devLimit = static_cast<VkDeviceSize>(pseudoDeviceLimit * 0.9);
	VkDeviceSize resSize = getRequiredImageSize(&imageInfo);

	if (image->isMigratable && 
		memUsage == VMA_MEMORY_USAGE_GPU_ONLY && 
		totalDeviceUsage + resSize > devLimit)
	{
		VkDeviceSize devUsage = totalDeviceUsage + resSize;
		VkDeviceSize oldSpace = devLimit - totalDeviceUsage;

		std::vector<Resource*> URNotTheFace;
		Resource* pSmallestResource = nullptr;
		VkDeviceSize smallestResourceSize = 0;
		
		while (devUsage > devLimit) {
			pSmallestResource = deviceHeap.top();
			smallestResourceSize = pSmallestResource->allocation->GetSize();

			if (smallestResourceSize >= resSize) {
				devUsage -= resSize;
				break;
			}

			deviceHeap.pop();
			URNotTheFace.push_back(pSmallestResource);
			devUsage -= smallestResourceSize;
		}

		// push back because migrate:f(x) will mess with pop op.
		// but temp vector is now sorted from small to big
		for (Resource* res : URNotTheFace)
			deviceHeap.push(res);

		if (devLimit - devUsage < oldSpace) { // worthier
			for (Resource* res : URNotTheFace) {
				migrateResource(*res);
			}
			std::cout << "Still go for device" << std::endl;
		}
		else { // not worth
			// new one go to host
			memUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
			imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
			resSize = getRequiredImageSize(&imageInfo);
			std::cout << "Image go for host instead" << std::endl;
		}
	}

	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = memUsage;
	allocCreateInfo.flags = (pPersistentlyMappedData != nullptr) ? VMA_ALLOCATION_CREATE_MAPPED_BIT : NULL;

	VmaAllocationInfo allocInfo = {};
	
	VK_CHECK_RESULT(vmaCreateImage(allocator, &imageInfo, &allocCreateInfo, &image->image, &image->allocation, &allocInfo));

	image->width = width;
	image->height = height;
	image->format = format;
	image->lastImgLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	image->usage = usage;

	if (memUsage == VMA_MEMORY_USAGE_GPU_ONLY) {
		totalDeviceUsage += resSize;
		image->isInGPU = true;

		if (image->isMigratable) deviceHeap.push(image);
	}
	else {
		totalHostUsage += resSize;
		image->isInGPU = false;

		if (image->isMigratable) hostHeap.push(image);

		if (pPersistentlyMappedData != nullptr)
			*pPersistentlyMappedData = allocInfo.pMappedData;
	}

}

void ResourceManager::createImageInDevice(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, Image *image, void * pData, VkDeviceSize imageSize)
{
	if (pData == nullptr)
		throw std::invalid_argument("f(x):createBufferInDevice needs data to initiate.");

	if (usage & VK_IMAGE_USAGE_SAMPLED_BIT && format == VK_FORMAT_R8G8B8A8_UNORM)
		usage = usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	Buffer stagingBuffer;

	void* pMappedData;

	createBuffer(
		VMA_MEMORY_USAGE_CPU_ONLY,
		imageSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		&stagingBuffer,
		&pMappedData
	);

	memcpy(pMappedData, pData, imageSize);

	createImage(
		VMA_MEMORY_USAGE_GPU_ONLY,
		width,
		height,
		format,
		usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		image,
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

	destroyBuffer(stagingBuffer);
}

void ResourceManager::createImageInHost(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, Image *image, void * pData)
{
	if ((usage & VK_IMAGE_USAGE_SAMPLED_BIT) && (pData == nullptr))
		usage = usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	void* pMappedData;
	VkDeviceSize imageSize = width * height * 4;

	createImage(
		VMA_MEMORY_USAGE_CPU_TO_GPU,
		width,
		height,
		format,
		usage,
		image,
		(pData == nullptr) ? nullptr : &pMappedData
	);

	if (pData != nullptr)
		memcpy(pMappedData, pData, imageSize);

	beginCmdBuffer();

	transitionImageLayout(cmdBuffer, image->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	flushCmdBuffer();
}

void ResourceManager::mapMemory(VmaAllocation allocation, void ** ppData)
{
	VK_CHECK_RESULT(vmaMapMemory(allocator,allocation,ppData));
}

void ResourceManager::unmapMemory(VmaAllocation allocation)
{
	vmaUnmapMemory(allocator, allocation);
}

void ResourceManager::destroyBuffer(Buffer &buffer)
{
	if (buffer.isInGPU)
		totalDeviceUsage -= buffer.allocation->GetSize();
	else 
		totalHostUsage -= buffer.allocation->GetSize();

	vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
}

void ResourceManager::destroyImage(Image &image)
{
	if (image.isInGPU) {
		totalDeviceUsage -= image.allocation->GetSize();
		deviceHeap.remove(&image);
		vmaDestroyImage(allocator, image.image, image.allocation);
		
		//Resource* pRes;
		VkDeviceSize devLimit = static_cast<VkDeviceSize>(pseudoDeviceLimit * 0.9);
		while (!hostHeap.empty()) {
			if (hostHeap.top()->allocation->GetSize() + totalDeviceUsage > devLimit) break;

			//pRes = hostHeap.top();
			//hostHeap.pop();

			migrateResource(*hostHeap.top());

			//deviceHeap.push(pRes);
			// after this, top has been poped, totalDev is added
		}
		
	}
	else {
		totalHostUsage -= image.allocation->GetSize();
		hostHeap.remove(&image);
		vmaDestroyImage(allocator, image.image, image.allocation);
	}
	
	//vmaDestroyImage(allocator, image.image, image.allocation);
}

void ResourceManager::createImageView(VkImage image, VkFormat format, VkImageView * imageView)
{
	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, imageView));
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
	/*
	if (texture.isInGPU) {
		memUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		texture.isInGPU = false;
		totalDeviceUsage -= texture.allocation->GetSize();
		deviceHeap.remove(&texture);

		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProperties);
		if (!(formatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
			throw std::runtime_error("Secondary Image doesn't support desired format.");
	}
	else {
		memUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		texture.isInGPU = true;
		totalHostUsage -= texture.allocation->GetSize();
		hostHeap.remove(&texture);
	}

	

	
	createImage(
		memUsage,
		texture.width,
		texture.height,
		texture.format,
		texture.usage,
		&destImage,
		nullptr
	);
	*/
	// edit start

	VkImage dstImage;
	VmaAllocation dstAlloc;

	VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = texture.width;
	imageInfo.extent.height = texture.height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = texture.format;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = texture.usage;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocCreateInfo = {};

	if (texture.isInGPU) {
		allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
		texture.isInGPU = false;

		totalDeviceUsage -= texture.allocation->GetSize();
		deviceHeap.remove(&texture);

		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(physicalDevice, texture.format, &formatProperties);
		if (!(formatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
			throw std::runtime_error("Secondary Image doesn't support desired format.");
	}
	else {
		allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		texture.isInGPU = true;

		totalHostUsage -= texture.allocation->GetSize();
		hostHeap.remove(&texture);
	}

	VK_CHECK_RESULT(vmaCreateImage(allocator, &imageInfo, &allocCreateInfo, &dstImage, &dstAlloc, nullptr));

	//edit end

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

	vmaDestroyImage(allocator, texture.image, texture.allocation);

	texture.image = dstImage;
	texture.allocation = dstAlloc;
	texture.lastImgLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	texture.isBoundToDesc = false;

	vkDestroyImageView(device, texture.view, nullptr);
	createImageView(texture.image, texture.format, &texture.view);

	texture.updateDescriptorInfo();

	if (texture.isInGPU) {
		totalDeviceUsage += texture.allocation->GetSize();
		deviceHeap.push(&texture);
	}
	else {
		totalHostUsage += texture.allocation->GetSize();
		hostHeap.push(&texture);
	}
}

void ResourceManager::migrateBuffer(Buffer & buffer)
{
	// deprecate and should not be used this entire f(x)
	if (buffer.buffer == NULL)
		throw std::runtime_error("Destination texture param does not exist.");

	VmaMemoryUsage memUsage;
	if (buffer.isInGPU) {
		memUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		buffer.isInGPU = false;
	}
	else {
		memUsage = VMA_MEMORY_USAGE_GPU_ONLY;
		buffer.isInGPU = true;
	}

	Buffer destBuffer;

	createBuffer(
		memUsage,
		buffer.size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | buffer.usage,
		&destBuffer,
		nullptr
	);

	beginCmdBuffer();

	VkBufferCopy copyRegionB = {};
	copyRegionB.size = buffer.size;
	copyRegionB.srcOffset = 0;
	copyRegionB.dstOffset = 0;

	vkCmdCopyBuffer(cmdBuffer, buffer.buffer, destBuffer.buffer, 1, &copyRegionB);

	flushCmdBuffer();

	destroyBuffer(buffer);

	buffer.allocation = destBuffer.allocation;
	buffer.buffer = destBuffer.buffer;
	buffer.isBoundToDesc = false;

	buffer.updateDescriptorInfo();
}

void ResourceManager::migrateResource(Resource & resource)
{
	if (!resource.isMigratable) 
		throw std::invalid_argument("You are moving immigratable resource.");


	if (resource.type == RESOURCE_TYPE_BUFFER)
		migrateBuffer((Buffer&)resource);
	else if (resource.type == RESOURCE_TYPE_IMAGE)
		migrateTexture((Image&)resource);
	else
		throw std::invalid_argument("Cannot indentify resource type.");

}

void ResourceManager::reduceMemoryBound(VkDeviceSize amount)
{
	pseudoDeviceLimit -= amount;
	std::cout << "Now Max Device Memory is " << pseudoDeviceLimit << std::endl;

	VkDeviceSize devLimit = static_cast<VkDeviceSize>(pseudoDeviceLimit * 0.9);
	while (!deviceHeap.empty() && devLimit < totalDeviceUsage)
	{
		migrateResource(*deviceHeap.top());
		std::cout << "Migrate one time" << std::endl;
	}
	std::cout << "Device Heap size : " << deviceHeap.size() << std::endl;
	std::cout << "Host Heap size : " << hostHeap.size() << std::endl;
	std::cout << "Now Total Device Usage is " << totalDeviceUsage << std::endl;
	std::cout << "Now Total Host Usage is " << totalHostUsage << std::endl;
}

void ResourceManager::extendMemoryBound(VkDeviceSize amount)
{
	pseudoDeviceLimit += amount;
	std::cout << "Now Max Device Memory is " << pseudoDeviceLimit << std::endl;

	VkDeviceSize devLimit = static_cast<VkDeviceSize>(pseudoDeviceLimit * 0.9);
	while (!hostHeap.empty()) 
	{
		if (hostHeap.top()->allocation->GetSize() + totalDeviceUsage > devLimit) break;

		migrateResource(*hostHeap.top());
	}
	std::cout << "Device Heap size : " << deviceHeap.size() << std::endl;
	std::cout << "Host Heap size : " << hostHeap.size() << std::endl;
	std::cout << "Now Total Device Usage is " << totalDeviceUsage << std::endl;
	std::cout << "Now Total Host Usage is " << totalHostUsage << std::endl;
}

void ResourceManager::inspectHeap()
{
	std::cout << "For Device : ";
	deviceHeap.checkMember();
	std::cout << "For Host : ";
	hostHeap.checkMember();
}

VkDeviceSize ResourceManager::getRequiredImageSize(VkImageCreateInfo* info)
{
	VkImage pseudoImage;
	VkMemoryRequirements memReqs;
	VK_CHECK_RESULT(vkCreateImage(device, info, nullptr, &pseudoImage));
	vkGetImageMemoryRequirements(device, pseudoImage, &memReqs);
	vkDestroyImage(device, pseudoImage, nullptr);

	return memReqs.size;
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

bool gtResource::operator()(Resource * const &lhs, Resource * const &rhs)
{
	return lhs->allocation->GetSize() > rhs->allocation->GetSize();
}

bool ResourceHeap::remove(const Resource * value)
{
	if (value == this->top()) {
		this->pop();
		return true;
	}
	else {
		auto it = std::find(this->c.begin(), this->c.end(), value);
		if (it != this->c.end()) {
			this->c.erase(it);
			std::make_heap(this->c.begin(), this->c.end(), this->comp);
			return true;
		}
		else {
			return false;
		}
	}
}

void ResourceHeap::checkMember()
{
	for (auto e : this->c) {
		if (e->type == RESOURCE_TYPE_IMAGE)
			std::cout << "exist ";
		else 
			std::cout << "nah ";
	}
	std::cout << std::endl;
}

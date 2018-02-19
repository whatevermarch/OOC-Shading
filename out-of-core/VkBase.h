#pragma once

#include "VkUtils.h"

#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "ResourceManager.h"

#define DEFAULT_FENCE_TIMEOUT 100000000000


struct QueueFamilyIndices {
	int graphicsFamily = -1;
	int presentFamily = -1;

	bool isComplete() {
		return graphicsFamily >= 0 && presentFamily >= 0;
	}
};

struct SwapChainSupportDetails {
	VkSurfaceCapabilitiesKHR capabilities;
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> presentModes;
};

struct Vertex {
	float pos[2];
	float color[3];
	float texCoord[2];

	static std::array<VkVertexInputBindingDescription, 1> getBindingDescriptions() {
		std::array<VkVertexInputBindingDescription, 1> bindingDescriptions = {};
		bindingDescriptions[0].binding = 0;
		bindingDescriptions[0].stride = sizeof(Vertex);
		bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		return bindingDescriptions;
	}

	static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
		std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions = {};

		attributeDescriptions[0].binding = 0;
		attributeDescriptions[0].location = 0;
		attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
		attributeDescriptions[0].offset = offsetof(Vertex, pos);

		attributeDescriptions[1].binding = 0;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[1].offset = offsetof(Vertex, color);

		attributeDescriptions[2].binding = 0;
		attributeDescriptions[2].location = 2;
		attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
		attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

		return attributeDescriptions;
	}
};

class VkBase
{
public:
	void run(int width, int height, const char* appTitle);

	virtual ~VkBase();

protected:
	uint32_t screenWidth = 800;
	uint32_t screenHeight = 600;

	bool prepared = false;

	// window handle
	GLFWwindow *window;

	VkInstance instance;

	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties deviceProperties;
	VkPhysicalDeviceFeatures deviceFeatures;
	VkPhysicalDeviceMemoryProperties deviceMemoryProperties;

	VkFormat depthFormat;

	VkDevice device;

	ResourceManager *resMan;

	VkCommandPool cmdPool;

	std::vector<VkCommandBuffer> drawCmdBuffers;

	VkPipelineCache pipelineCache;

	struct {
		// graphic queue
		VkQueue graphic;
		// presentation queue
		VkQueue present;
	} stdQueues;

	struct {
		// Swap chain image presentation
		VkSemaphore presentComplete;
		// Command buffer submission and execution
		VkSemaphore renderComplete;
		// Text overlay submission and execution
		// VkSemaphore textOverlayComplete;
	} stdSemaphores;
	
	struct {
		VkSwapchainKHR handle;
		std::vector<VkImage> images;
		VkFormat imageFormat;
		VkExtent2D extent;
		std::vector<VkImageView> imageViews;
		std::vector<VkFramebuffer> framebuffers;
	} swapChain;

	struct {
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
	} depthStencil;

	virtual void setupInputHndCallback() = 0;

	virtual void prepare();

	virtual void render() = 0;

	// support functions
	void createImageView(VkImage image, VkFormat format, VkImageView* imgView);
	void createSampler(VkSampler* sampler);

	void transitionImgLayout(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);

	uint32_t getMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties);
	VkCommandBuffer createCmdBuffer(bool begin);
	void flushCmdBuffer(VkCommandBuffer cmdBuffer);
	VkShaderModule loadSPIRVShader(std::string filePath);

private:
	VkSurfaceKHR surface;

	VkDebugReportCallbackEXT debReportClbk;

	struct {
		uint32_t graphic;
		uint32_t present;
	} queueFamilyIndices;

	void initWindow(int width, int height, const char* appTitle);
	void initVulkan();

	void setupInstance();
	void setupDebugCallback();
	void setupSurface();
	void setupDevice();
	void setupResourceManager();
	
	void createSwapChain();
	void createCommandPool();
	void createDrawCmdBuffer();
	void createStandardSemaphores();
	void createPipelineCache();

	void renderLoop();
	// void fpsCounter();

	// support functions
	bool checkValidationLayerSupport();
	std::vector<const char*> getRequiredExtensions();
	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugReportFlagsEXT flags,
		VkDebugReportObjectTypeEXT objType,
		uint64_t obj,
		size_t location,
		int32_t code,
		const char* layerPrefix,
		const char* msg,
		void* userData
	);
	bool isDeviceSuitable(VkPhysicalDevice device);
	QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
	bool checkDeviceExtensionSupport(VkPhysicalDevice device);
	SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
	VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
	VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> availablePresentModes);
	VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
	VkFormat getSupportedDepthFormat(VkPhysicalDevice physicalDevice);

};


#pragma once

#include "VkUtils.h"

#include <GLFW/glfw3.h>

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
	uint32_t getMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties);
	VkCommandBuffer createCmdBuffer(bool begin);
	void flushCmdBuffer(VkCommandBuffer cmdBuffer);
	VkShaderModule loadSPIRVShader(std::string filePath);

	void createDrawCmdBuffer();

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


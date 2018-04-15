#pragma once

#include "VkUtils.h"

#include "ResourceManager.h"

#include "stb_font_consolas_24_usascii.inl"

// Defines for the STB font used
// STB font files can be found at http://nothings.org/stb/font/
#define STB_FONT_NAME stb_font_consolas_24_usascii
#define STB_FONT_WIDTH STB_FONT_consolas_24_usascii_BITMAP_WIDTH
#define STB_FONT_HEIGHT STB_FONT_consolas_24_usascii_BITMAP_HEIGHT 
#define STB_FIRST_CHAR STB_FONT_consolas_24_usascii_FIRST_CHAR
#define STB_NUM_CHARS STB_FONT_consolas_24_usascii_NUM_CHARS

// Max. number of chars the text overlay buffer can hold
#define TEXTOVERLAY_MAX_CHAR_COUNT 2048

class TextOverlay
{
public:
	TextOverlay(VkDevice device,
		VkCommandPool cmdPool,
		ResourceManager *resMan,
		std::vector<VkFramebuffer> &framebuffers,
		VkFormat colorformat,
		VkFormat depthformat,
		uint32_t *framebufferwidth,
		uint32_t *framebufferheight,
		std::vector<VkPipelineShaderStageCreateInfo> shaderstages);

	virtual ~TextOverlay();

	enum TextAlign { alignLeft, alignCenter, alignRight };

	bool visible = true;

	void beginTextUpdate();
	void addText(std::string text, float x, float y, TextAlign align);
	void endTextUpdate();

	void updateCommandBuffers();

	void submit(VkQueue queue, uint32_t bufferindex, VkSemaphore &waitSemaphore);

	inline VkSemaphore* getTextOverlaySemaphorePtr() { return &textOverlayComplete; };

private:
	VkDevice device;
	ResourceManager* resMan;

	// VkQueue queue; include in resMan
	VkFormat colorFormat;
	VkFormat depthFormat;

	uint32_t *frameBufferWidth;
	uint32_t *frameBufferHeight;

	VkSampler sampler;

	VkSemaphore textOverlayComplete;

	// will be replace by my object
	//VkImage image;
	//VkImageView view;
	//VkBuffer buffer;
	//VkDeviceMemory memory;
	//VkDeviceMemory imageMemory;

	Buffer vertexBuffer;
	Image fontTexture;

	VkDescriptorPool descriptorPool;
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorSet descriptorSet;
	VkPipelineLayout pipelineLayout;
	VkPipelineCache pipelineCache;
	VkPipeline pipeline;
	VkRenderPass renderPass;
	VkCommandPool commandPool;
	std::vector<VkCommandBuffer> cmdBuffers;
	std::vector<VkFramebuffer*> frameBuffers;
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

	// Pointer to mapped vertex buffer
	glm::vec4 *mapped = nullptr;

	stb_fontchar stbFontData[STB_NUM_CHARS];
	uint32_t numLetters;

	void prepareCmdBuffers();
	void prepareResources();
	void preparePipeline();
	void prepareRenderPass();
};


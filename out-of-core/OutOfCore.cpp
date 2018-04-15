#include "VkBase.h"
#include "Scene.h"

#define MEMORY_BOUND_CHANGE_SIZE_MB 10

const VkDeviceSize memoryBoundChangeSize = MEMORY_BOUND_CHANGE_SIZE_MB * 1000000;

const char* vertShaderFile = "shaders/scene.vert.spv";
const char* fragShaderFile = "shaders/scene.frag.spv";
const char* sampleTextureFile = "textures/marsha.jpg";

class VkApp : public VkBase 
{
public:
	virtual ~VkApp()
	{
		delete(scene);
		vkDestroyRenderPass(device, renderPass, nullptr);
	}

private:

	Buffer vertexBuffer;

	Buffer indexBuffer;
	uint32_t indicesCount;

	// Uniform buffer block object
	Buffer uniformBufferVS;

	Scene *scene;

	struct {
		glm::mat4 projectionMatrix;
		glm::mat4 modelMatrix;
		glm::mat4 viewMatrix;
	} uboVS;

	// On Device
	Image sampleTexture;
	// On Host
	Image secondaryTexture;

	VkFence swapComplete;

	VkDescriptorSetLayout descSetLayout;

	VkPipelineLayout pipelineLayout;

	VkPipeline pipeline;

	VkDescriptorPool descPool;

	VkDescriptorSet descSet;

	void setupInputHndCallback()
	{
		glfwSetWindowUserPointer(window, this);
		glfwSetKeyCallback(window, keyInput);
	}

	inline static void keyInput(GLFWwindow* window, int key, int scancode, int action, int mods) {
		VkApp *app = static_cast<VkApp*>(glfwGetWindowUserPointer(window));
		
		if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
			app->getHeapInfo();
		}
		else if (key == GLFW_KEY_LEFT && action == GLFW_PRESS) {
			app->reduceMemoryBound();
		}
		else if (key == GLFW_KEY_RIGHT && action == GLFW_PRESS) {
			app->extendMemoryBound();
		}
	}

	void getHeapInfo() {
		resMan->inspectHeap();
	}

	void reduceMemoryBound() {
		resMan->reduceMemoryBound(memoryBoundChangeSize);
		if(scene->rebindTexture())
			rebuildCommandBuffer();

		std::cout << "Reduce Memort Bound Completed!" << std::endl;
	}

	void extendMemoryBound() {
		resMan->extendMemoryBound(memoryBoundChangeSize);
		if (scene->rebindTexture())
			rebuildCommandBuffer();

		std::cout << "Extend Memort Bound Completed!" << std::endl;
	}

	void migrateTexture()
	{
		resMan->migrateTexture(sampleTexture);

		std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};
		// Binding 0 : Uniform buffer
		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[0].dstSet = descSet;
		descriptorWrites[0].dstBinding = 0;
		descriptorWrites[0].dstArrayElement = 0;
		descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrites[0].descriptorCount = 1;
		descriptorWrites[0].pBufferInfo = &uniformBufferVS.descInfo; //&bufferInfo;
		// Binding 1 : Sampler
		descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[1].dstSet = descSet;
		descriptorWrites[1].dstBinding = 1;
		descriptorWrites[1].dstArrayElement = 0;
		descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrites[1].descriptorCount = 1;
		descriptorWrites[1].pImageInfo = &sampleTexture.descInfo; //&imageInfo;
		// descSet need to be in prompt state ( finish used by previous frame ) in order to update.
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);

		for (short i = 0; i < static_cast<short>(drawCmdBuffers.size()); i++)
			vkResetCommandBuffer(drawCmdBuffers[i], VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

		buildCommandBuffers();

		std::cout << "Migration Completed! New Image is " << sampleTexture.image << std::endl;
	}

	void loadAsset()
	{
		scene = new Scene(device, stdQueues.graphic, resMan);
		scene->import("models/nanosuit/nanosuit.obj");

		updateUniformBuffers();
	}

	void updateUniformBuffers()
	{
		scene->uniformData.projection = glm::mat4(
			1.0f, 0.0f, 0.0f, 0.0f,
			+0.0f, -1.0f, 0.0f, 0.0f,
			+0.0f, 0.0f, 0.5f, 0.0f,
			+0.0f, 0.0f, 0.5f, 1.0f
		) * glm::perspective(glm::radians(60.0f), (float)screenWidth / (float)screenHeight, 0.1f, 256.0f);

		scene->uniformData.view = glm::lookAt(glm::vec3(10.0f, 10.0f, 10.0f), glm::vec3(0.0f, 8.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		scene->uniformData.model = glm::mat4(1.0f);

		scene->uniformData.model = glm::scale(scene->uniformData.model, glm::vec3(0.3f, 0.3f, 0.3f));

		void *pData;

		resMan->mapMemory(scene->uniformBuffer.allocation, &pData);
		memcpy(pData, &scene->uniformData, sizeof(scene->uniformData));
		resMan->unmapMemory(scene->uniformBuffer.allocation);
	}

	void setupDescriptorSetLayout()
	{
		// Binding 0: Uniform buffer (Vertex shader)
		VkDescriptorSetLayoutBinding uboLayoutBinding = {};
		uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboLayoutBinding.binding = 0;
		uboLayoutBinding.descriptorCount = 1;
		uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		uboLayoutBinding.pImmutableSamplers = nullptr;

		// Binding 1: Sampler (Fragment shader)
		VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
		samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerLayoutBinding.binding = 1;
		samplerLayoutBinding.descriptorCount = 1;
		samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		samplerLayoutBinding.pImmutableSamplers = nullptr;

		std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };

		VkDescriptorSetLayoutCreateInfo descriptorLayout = {};
		descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		descriptorLayout.bindingCount = static_cast<uint32_t>(bindings.size());
		descriptorLayout.pBindings = bindings.data();

		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descSetLayout));

		// Create the pipeline layout that is used to generate the rendering pipelines that are based on this descriptor set layout
		// In a more complex scenario you would have different pipeline layouts for different descriptor set layouts that could be reused
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
		pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutCreateInfo.pNext = nullptr;
		pipelineLayoutCreateInfo.setLayoutCount = 1;
		pipelineLayoutCreateInfo.pSetLayouts = &descSetLayout;

		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}

	void preparePipelines()
	{
		// Create the graphics pipeline used in this example
		// Vulkan uses the concept of rendering pipelines to encapsulate fixed states, replacing OpenGL's complex state machine
		// A pipeline is then stored and hashed on the GPU making pipeline changes very fast
		// Note: There are still a few dynamic states that are not directly part of the pipeline (but the info that they are used is)

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
		pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		// The layout used for this pipeline (can be shared among multiple pipelines using the same layout)
		pipelineCreateInfo.layout = scene->pipelineLayout;
		// Renderpass this pipeline is attached to
		pipelineCreateInfo.renderPass = renderPass;

		// Construct the differnent states making up the pipeline

		// Input assembly state describes how primitives are assembled
		// This pipeline will assemble vertex data as a triangle lists (though we only use one triangle)
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
		inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		// Rasterization state
		VkPipelineRasterizationStateCreateInfo rasterizationState = {};
		rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizationState.depthClampEnable = VK_FALSE;
		rasterizationState.rasterizerDiscardEnable = VK_FALSE;
		rasterizationState.depthBiasEnable = VK_FALSE;
		rasterizationState.lineWidth = 1.0f;

		// Color blend state describes how blend factors are calculated (if used)
		// We need one blend attachment state per color attachment (even if blending is not used
		VkPipelineColorBlendAttachmentState blendAttachmentState[1] = {};
		blendAttachmentState[0].colorWriteMask = 0xf;
		blendAttachmentState[0].blendEnable = VK_FALSE;
		VkPipelineColorBlendStateCreateInfo colorBlendState = {};
		colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlendState.attachmentCount = 1;
		colorBlendState.pAttachments = blendAttachmentState;

		// Viewport state sets the number of viewports and scissor used in this pipeline
		// Note: This is actually overriden by the dynamic states (see below)
		VkPipelineViewportStateCreateInfo viewportState = {};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		// Enable dynamic states
		// Most states are baked into the pipeline, but there are still a few dynamic states that can be changed within a command buffer
		// To be able to change these we need do specify which dynamic states will be changed using this pipeline. Their actual states are set later on in the command buffer.
		// For this example we will set the viewport and scissor using dynamic states
		std::vector<VkDynamicState> dynamicStateEnables;
		dynamicStateEnables.push_back(VK_DYNAMIC_STATE_VIEWPORT);
		dynamicStateEnables.push_back(VK_DYNAMIC_STATE_SCISSOR);
		VkPipelineDynamicStateCreateInfo dynamicState = {};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.pDynamicStates = dynamicStateEnables.data();
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

		// Depth and stencil state containing depth and stencil compare and test operations
		// We only use depth tests and want depth tests and writes to be enabled and compare with less or equal
		VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
		depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencilState.depthTestEnable = VK_TRUE;
		depthStencilState.depthWriteEnable = VK_TRUE;
		depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthStencilState.depthBoundsTestEnable = VK_FALSE;
		depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
		depthStencilState.back.passOp = VK_STENCIL_OP_KEEP;
		depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;
		depthStencilState.stencilTestEnable = VK_FALSE;
		depthStencilState.front = depthStencilState.back;

		// Multi sampling state
		// This example does not make use fo multi sampling (for anti-aliasing), the state must still be set and passed to the pipeline
		VkPipelineMultisampleStateCreateInfo multisampleState = {};
		multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisampleState.pSampleMask = nullptr;

		// Vertex input descriptions 
		// Specifies the vertex input parameters for a pipeline
		auto bindingDesc = Vertex::getBindingDescriptions();
		auto attribDesc = Vertex::getAttributeDescriptions();

		// Vertex input state used for pipeline creation
		VkPipelineVertexInputStateCreateInfo vertexInputState = {};
		vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDesc.size());
		vertexInputState.pVertexBindingDescriptions = bindingDesc.data();
		vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribDesc.size());
		vertexInputState.pVertexAttributeDescriptions = attribDesc.data();

		// Shaders
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

		// Vertex shader
		shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		// Set pipeline stage for this shader
		shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		// Load binary SPIR-V shader
		shaderStages[0].module = loadSPIRVShader(vertShaderFile);
		// Main entry point for the shader
		shaderStages[0].pName = "main";
		assert(shaderStages[0].module != VK_NULL_HANDLE);

		// Fragment shader
		shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		// Set pipeline stage for this shader
		shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		// Load binary SPIR-V shader
		shaderStages[1].module = loadSPIRVShader(fragShaderFile);
		// Main entry point for the shader
		shaderStages[1].pName = "main";
		assert(shaderStages[1].module != VK_NULL_HANDLE);

		// Set pipeline shader stage info
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCreateInfo.pStages = shaderStages.data();

		// Assign the pipeline states to the pipeline creation info structure
		pipelineCreateInfo.pVertexInputState = &vertexInputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.renderPass = renderPass;
		pipelineCreateInfo.pDynamicState = &dynamicState;

		// Create rendering pipeline using the specified states
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &scene->pipelines.solid));

		// Alpha blended pipeline
		rasterizationState.cullMode = VK_CULL_MODE_NONE;
		blendAttachmentState[0].blendEnable = VK_TRUE;
		blendAttachmentState[0].colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		blendAttachmentState[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &scene->pipelines.blending));

		// Shader modules are no longer needed once the graphics pipeline has been created
		vkDestroyShaderModule(device, shaderStages[0].module, nullptr);
		vkDestroyShaderModule(device, shaderStages[1].module, nullptr);
	}

	void setupDescriptorPool()
	{
		// We need to tell the API the number of max. requested descriptors per type
		std::array<VkDescriptorPoolSize, 2> poolSizes = {};

		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = 1;

		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[1].descriptorCount = 1;

		// Create the global descriptor pool
		// All descriptors used in this example are allocated from this pool
		VkDescriptorPoolCreateInfo descriptorPoolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		descriptorPoolInfo.pPoolSizes = poolSizes.data();
		// Set the max. number of descriptor sets that can be requested from this pool (requesting beyond this limit will result in an error)
		descriptorPoolInfo.maxSets = 1;

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descPool));
	}

	void setupDescriptorSet()
	{
		// Allocate a new descriptor set from the global descriptor pool
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = descPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &descSetLayout;

		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descSet));

		// Update the descriptor set determining the shader binding points
		// For every binding point used in a shader there needs to be one
		// descriptor set matching that binding point

		VkDescriptorBufferInfo bufferInfo = {};
		bufferInfo.buffer = uniformBufferVS.buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(uboVS);

		VkDescriptorImageInfo imageInfo = {};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = sampleTexture.view;
		imageInfo.sampler = sampleTexture.sampler;

		std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};
		// Binding 0 : Uniform buffer
		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[0].dstSet = descSet;
		descriptorWrites[0].dstBinding = 0;
		descriptorWrites[0].dstArrayElement = 0;
		descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrites[0].descriptorCount = 1;
		descriptorWrites[0].pBufferInfo = &bufferInfo;
		// Binding 1 : Sampler
		descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[1].dstSet = descSet;
		descriptorWrites[1].dstBinding = 1;
		descriptorWrites[1].dstArrayElement = 0;
		descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrites[1].descriptorCount = 1;
		descriptorWrites[1].pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = {};
		cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		cmdBufInfo.pNext = nullptr;

		// Set clear values for all framebuffer attachments with loadOp set to clear
		// We use two attachments (color and depth) that are cleared at the start of the subpass and as such we need to set clear values for both
		VkClearValue clearValues[2];
		clearValues[0].color = { { 0.25f, 0.25f, 0.25f, 1.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = {};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.pNext = nullptr;
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = screenWidth;
		renderPassBeginInfo.renderArea.extent.height = screenHeight;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = swapChain.framebuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			// Start the first sub pass specified in our default render pass setup by the base class
			// This will clear the color and depth attachment
			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			// Update dynamic viewport state
			VkViewport viewport = {};
			viewport.height = (float)screenHeight;
			viewport.width = (float)screenWidth;
			viewport.minDepth = (float) 0.0f;
			viewport.maxDepth = (float) 1.0f;
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			// Update dynamic scissor state
			VkRect2D scissor = {};
			scissor.extent.width = screenWidth;
			scissor.extent.height = screenHeight;
			scissor.offset.x = 0;
			scissor.offset.y = 0;
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			scene->render(drawCmdBuffers[i]);
			vkCmdEndRenderPass(drawCmdBuffers[i]);

			// Ending the render pass will add an implicit barrier transitioning the frame buffer color attachment to 
			// VK_IMAGE_LAYOUT_PRESENT_SRC_KHR for presenting it to the windowing system

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void rebuildCommandBuffer()
	{
		for (short i = 0; i < static_cast<short>(drawCmdBuffers.size()); i++)
			vkResetCommandBuffer(drawCmdBuffers[i], VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

		buildCommandBuffers();
	}

	virtual void prepare()
	{
		VkBase::prepare();
		loadAsset();
		preparePipelines();
		buildCommandBuffers();
		prepared = true;
	}

	void draw()
	{
		prepareFrame();

		// Pipeline stage at which the queue submission will wait (via pWaitSemaphores)
		VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		// The submit info structure specifices a command buffer queue submission batch
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.pWaitDstStageMask = &waitStageMask;									// Pointer to the list of pipeline stages that the semaphore waits will occur at
		submitInfo.pWaitSemaphores = &stdSemaphores.presentComplete;							// Semaphore(s) to wait upon before the submitted command buffer starts executing
		submitInfo.waitSemaphoreCount = 1;												// One wait semaphore																				
		submitInfo.pSignalSemaphores = &stdSemaphores.renderComplete;						// Semaphore(s) to be signaled when command buffers have completed
		submitInfo.signalSemaphoreCount = 1;											// One signal semaphore
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];					// Command buffers(s) to execute in this batch (submission)
		submitInfo.commandBufferCount = 1;	// One command buffer

		// Submit to the graphics queue passing no wait fence
		VK_CHECK_RESULT(vkQueueSubmit(stdQueues.graphic, 1, &submitInfo, VK_NULL_HANDLE));

		submitFrame();
	}

	void render()
	{
		if (prepared == true)
			draw();
		return;


	}
	
};

VkApp *app;

int main() 
{
	app = new VkApp();

	try {
		app->run(800, 600, "Vulkan Test");
	}
	catch (const std::runtime_error& e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

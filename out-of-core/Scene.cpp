#include "Scene.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

Scene::Scene(VkDevice device, VkQueue queue, ResourceManager *resMan) : device(device), queue(queue), resMan(resMan)
{
	createSampler(&defaultSampler);
	resMan->createBufferInHost(sizeof(uniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &uniformBuffer, nullptr);
	uniformBuffer.updateDescriptorInfo();

}


Scene::~Scene()
{
	resMan->destroyBuffer(vertexBuffer);
	resMan->destroyBuffer(indexBuffer);
	for (auto material : materials)
	{
		vkDestroyImageView(device, material.diffuse.image.view, nullptr);
		resMan->destroyImage(material.diffuse.image);
	}
	vkDestroySampler(device, defaultSampler, nullptr);
	vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.material, nullptr);
	vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);
	vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	vkDestroyPipeline(device, pipelines.solid, nullptr);
	vkDestroyPipeline(device, pipelines.blending, nullptr);
	resMan->destroyBuffer(uniformBuffer);

}

void Scene::import(const std::string & filePath)
{
	Assimp::Importer importer;
	const aiScene *scene = importer.ReadFile(filePath, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals );

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		throw std::runtime_error("Cannot load input model!");
	}
	assetPath = filePath.substr(0, filePath.find_last_of('/'));
	assetPath.append("/");
	std::cout << "Asset Path is : " << assetPath << std::endl;

	//processNode(scene->mRootNode);
	extractMaterials(scene);
	extractMeshes(scene);

}

void Scene::render(VkCommandBuffer cmdBuffer)
{
	VkDeviceSize offsets[1] = { 0 };

	// Bind scene vertex and index buffers
	vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vertexBuffer.buffer, offsets);
	vkCmdBindIndexBuffer(cmdBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	for (size_t i = 0; i < meshes.size(); i++)
	{
		// We will be using multiple descriptor sets for rendering
		// In GLSL the selection is done via the set and binding keywords
		// VS: layout (set = 0, binding = 0) uniform UBO;
		// FS: layout (set = 1, binding = 0) uniform sampler2D samplerColorMap;

		std::array<VkDescriptorSet, 2> descriptorSets;
		// Set 0: Scene descriptor set containing global matrices
		descriptorSets[0] = descriptorSetScene;
		// Set 1: Per-Material descriptor set containing bound images
		descriptorSets[1] = meshes[i].material->descriptorSet;

		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *meshes[i].material->pipeline);
		vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, static_cast<uint32_t>(descriptorSets.size()), descriptorSets.data(), 0, NULL);

		// Pass material properies via push constants
		vkCmdPushConstants(
			cmdBuffer,
			pipelineLayout,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			0,
			sizeof(MaterialProperties),
			&meshes[i].material->properties);

		// Render from the global scene vertex buffer using the mesh index offset
		vkCmdDrawIndexed(cmdBuffer, meshes[i].indexCount, 1, 0, meshes[i].indexBase, 0);
	}

}

void Scene::rebindTexture()
{
	// Material descriptor sets
	for (size_t i = 0; i < materials.size(); i++)
	{
		if (materials[i].diffuse.image.isBoundToDesc) continue;

		std::vector<VkWriteDescriptorSet> descriptorWrites;
		descriptorWrites.resize(1);

		// Binding 0: Diffuse texture // this will be executed again when migration process complete
		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[0].dstSet = materials[i].descriptorSet;
		descriptorWrites[0].dstBinding = 0;
		descriptorWrites[0].dstArrayElement = 0;
		descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrites[0].descriptorCount = 1;
		descriptorWrites[0].pImageInfo = &materials[i].diffuse.image.descInfo;

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, NULL);
	}
}

void Scene::extractMeshes(const aiScene *scene)
{
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	uint32_t indexBase = 0;

	meshes.resize(scene->mNumMeshes);

	for (uint32_t i = 0; i < meshes.size(); i++)
	{
		aiMesh *aMesh = scene->mMeshes[i];

		std::cout << "Mesh \"" << aMesh->mName.C_Str() << "\"" << std::endl;
		std::cout << "	Material: \"" << materials[aMesh->mMaterialIndex].name << "\"" << std::endl;
		std::cout << "	Faces: " << aMesh->mNumFaces << std::endl;

		meshes[i].material = &materials[aMesh->mMaterialIndex];
		meshes[i].indexBase = indexBase;
		meshes[i].indexCount = aMesh->mNumFaces * 3;

		// Vertices
		bool hasUV = aMesh->HasTextureCoords(0);
		bool hasColor = aMesh->HasVertexColors(0);
		bool hasNormals = aMesh->HasNormals();

		for (uint32_t v = 0; v < aMesh->mNumVertices; v++)
		{
			Vertex vertex;
			vertex.pos = glm::vec3(aMesh->mVertices[v].x, aMesh->mVertices[v].y, aMesh->mVertices[v].z);
			//vertex.pos.y = -vertex.pos.y;
			vertex.uv = hasUV ? glm::vec2(aMesh->mTextureCoords[0][v].x, aMesh->mTextureCoords[0][v].y) : glm::vec2(0.0f);
			vertex.normal = hasNormals ? glm::vec3(aMesh->mNormals[v].x, aMesh->mNormals[v].y, aMesh->mNormals[v].z) : glm::vec3(0.0f);
			//vertex.normal.y = -vertex.normal.y;
			vertex.color = hasColor ? glm::vec3(aMesh->mColors[0][v].r, aMesh->mColors[0][v].g, aMesh->mColors[0][v].b) : glm::vec3(1.0f);
			vertices.push_back(vertex);
		}

		// Indices
		for (uint32_t f = 0; f < aMesh->mNumFaces; f++)
		{
			for (uint32_t j = 0; j < 3; j++)
			{
				indices.push_back(aMesh->mFaces[f].mIndices[j]);
			}
		}

		indexBase += aMesh->mNumFaces * 3;
	}

	uint32_t vertexDataSize = static_cast<uint32_t>(vertices.size() * sizeof(Vertex));
	uint32_t indexDataSize = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));

	resMan->createBufferInDevice(
		vertexDataSize,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		&vertexBuffer,
		vertices.data()
	);

	resMan->createBufferInDevice(
		indexDataSize,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		&indexBuffer,
		indices.data()
	);
}

void Scene::extractMaterials(const aiScene * scene)
{
	materials.resize(scene->mNumMaterials);

	for (size_t i = 0; i < materials.size(); i++)
	{
		materials[i] = {};

		aiString name;
		scene->mMaterials[i]->Get(AI_MATKEY_NAME, name);

		// Properties
		aiColor4D color;
		scene->mMaterials[i]->Get(AI_MATKEY_COLOR_AMBIENT, color);
		materials[i].properties.ambient = glm::vec4(color.r, color.g, color.b, color.a) + glm::vec4(0.1f);
		scene->mMaterials[i]->Get(AI_MATKEY_COLOR_DIFFUSE, color);
		materials[i].properties.diffuse = glm::vec4(color.r, color.g, color.b, color.a);
		scene->mMaterials[i]->Get(AI_MATKEY_COLOR_SPECULAR, color);
		materials[i].properties.specular = glm::vec4(color.r, color.g, color.b, color.a);
		scene->mMaterials[i]->Get(AI_MATKEY_OPACITY, materials[i].properties.opacity);

		if ((materials[i].properties.opacity) > 0.0f)
			materials[i].properties.specular = glm::vec4(0.0f);

		materials[i].name = name.C_Str();
		std::cout << "Material \"" << materials[i].name << "\" with index " << i << std::endl;

		// Textures
		// Determine texture format
		VkFormat texFormat = VK_FORMAT_R8G8B8A8_UNORM;

		aiString texturefile;
		// Diffuse
		scene->mMaterials[i]->GetTexture(aiTextureType_DIFFUSE, 0, &texturefile);
		if (scene->mMaterials[i]->GetTextureCount(aiTextureType_DIFFUSE) > 0)
		{
			std::cout << "  Diffuse: \"" << texturefile.C_Str() << "\"" << std::endl;
			std::string fileName = std::string(texturefile.C_Str());
			std::replace(fileName.begin(), fileName.end(), '\\', '/');
			loadTextureFromFile(assetPath + fileName, texFormat, &materials[i].diffuse);
			materials[i].diffuse.name = fileName;
			materials[i].diffuse.type = TEXTURE_TYPE_DIFFUSE;
		}
		else
		{
			std::cout << "  Material has no diffuse, using dummy texture!" << std::endl;
			loadTextureFromFile("models/dummy_texture.png", VK_FORMAT_R8G8B8A8_UNORM, &materials[i].diffuse);
			materials[i].diffuse.name = std::string("dummy_" + i);
			materials[i].diffuse.type = TEXTURE_TYPE_DIFFUSE;
		}

		// For scenes with multiple textures per material we would need to check for additional texture types, e.g.:
		// aiTextureType_HEIGHT, aiTextureType_OPACITY, aiTextureType_SPECULAR, etc.

		// Assign pipeline
		materials[i].pipeline = (materials[i].properties.opacity != 0.0f) ? &pipelines.solid : &pipelines.blending;
		//materials[i].pipeline = &pipelines.solid;
	}

	// Generate descriptor sets for the materials

	// Descriptor pool
	std::array<VkDescriptorPoolSize, 2> poolSizes = {};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[0].descriptorCount = 1;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[1].descriptorCount = static_cast<uint32_t>(materials.size());

	VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
	descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	descriptorPoolInfo.pPoolSizes = poolSizes.data();
	descriptorPoolInfo.maxSets = static_cast<uint32_t>(materials.size() + 1);

	VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

	// Descriptor set and pipeline layouts
	std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
	VkDescriptorSetLayoutCreateInfo descriptorLayout = {};
	setLayoutBindings.resize(1);

	// Set 0: Scene matrices
	setLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	setLayoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	setLayoutBindings[0].binding = 0;
	setLayoutBindings[0].descriptorCount = 1;

	descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorLayout.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
	descriptorLayout.pBindings = setLayoutBindings.data();

	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayouts.scene));

	// Set 1: Material data
	setLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	setLayoutBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	setLayoutBindings[0].binding = 0;
	setLayoutBindings[0].descriptorCount = 1;

	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayouts.material));

	// Setup pipeline layout
	std::array<VkDescriptorSetLayout, 2> setLayouts = { descriptorSetLayouts.scene, descriptorSetLayouts.material };
	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
	pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCreateInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
	pipelineLayoutCreateInfo.pSetLayouts = setLayouts.data();

	// We will be using a push constant block to pass material properties to the fragment shaders
	VkPushConstantRange pushConstantRange = {};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstantRange.size = sizeof(MaterialProperties);
	pushConstantRange.offset = 0;

	pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
	pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

	// Material descriptor sets
	for (size_t i = 0; i < materials.size(); i++)
	{
		// Descriptor set
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &descriptorSetLayouts.material;

		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &materials[i].descriptorSet));

		std::vector<VkWriteDescriptorSet> descriptorWrites;
		descriptorWrites.resize(1);

		// Binding 0: Diffuse texture // this will be executed again when migration process complete
		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[0].dstSet = materials[i].descriptorSet;
		descriptorWrites[0].dstBinding = 0;
		descriptorWrites[0].dstArrayElement = 0;
		descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrites[0].descriptorCount = 1;
		descriptorWrites[0].pImageInfo = &materials[i].diffuse.image.descInfo;

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, NULL);

		materials[i].diffuse.image.isBoundToDesc = true;
	}

	// Scene descriptor set
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &descriptorSetLayouts.scene;

	VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSetScene));

	std::vector<VkWriteDescriptorSet> descriptorWrites;
	descriptorWrites.resize(1);

	// Binding 0 : Vertex shader uniform buffer
	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].dstSet = descriptorSetScene;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].dstArrayElement = 0;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrites[0].descriptorCount = 1;
	descriptorWrites[0].pBufferInfo = &uniformBuffer.descInfo;

	vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, NULL);

}

void Scene::loadTextureFromFile(const std::string & fileName, VkFormat format, Texture * texture)
{
	/*
	for (std::string textureName : loadedTexture) {
		if (std::strcmp(textureName.data(), fileName.c_str()) == 0)
		{
			return;
		}
	}
	*/
	int texWidth, texHeight, texChannels;

	stbi_uc* pixels = stbi_load(fileName.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	if (!pixels) {
		throw std::runtime_error("failed to load texture image!");
	}

	resMan->createImageInDevice(
		texWidth,
		texHeight,
		format,
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		&texture->image,
		pixels
	);

	stbi_image_free(pixels);

	resMan->createImageView(texture->image.image, texture->image.format, &texture->image.view);
	texture->image.sampler = defaultSampler;
	texture->image.updateDescriptorInfo();

}

void Scene::createSampler(VkSampler * sampler)
{
	VkSamplerCreateInfo samplerInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = 16;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 0.0f;

	VK_CHECK_RESULT(vkCreateSampler(device, &samplerInfo, nullptr, sampler));
}


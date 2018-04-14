#pragma once

#include"VkUtils.h"
#include"ResourceManager.h"

#include<assimp\Importer.hpp>
#include<assimp\scene.h>
#include<assimp\postprocess.h>

struct Vertex {
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 uv;
	glm::vec3 normal;

	static std::array<VkVertexInputBindingDescription, 1> getBindingDescriptions() {
		std::array<VkVertexInputBindingDescription, 1> bindingDescriptions = {};
		bindingDescriptions[0].binding = 0;
		bindingDescriptions[0].stride = sizeof(Vertex);
		bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		return bindingDescriptions;
	}

	static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions() {
		std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions = {};

		attributeDescriptions[0].binding = 0;
		attributeDescriptions[0].location = 0;
		attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[0].offset = offsetof(Vertex, pos);

		attributeDescriptions[1].binding = 0;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[1].offset = offsetof(Vertex, color);

		attributeDescriptions[2].binding = 0;
		attributeDescriptions[2].location = 2;
		attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
		attributeDescriptions[2].offset = offsetof(Vertex, uv);

		attributeDescriptions[3].binding = 0;
		attributeDescriptions[3].location = 3;
		attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[3].offset = offsetof(Vertex, normal);

		return attributeDescriptions;
	}
};

enum TextureType
{
	TEXTURE_TYPE_AMBIENT = 0,
	TEXTURE_TYPE_DIFFUSE = 1,
	TEXTURE_TYPE_SPECULAR = 2
};

// Scene related structs

struct Texture
{
	Image image;
	std::string name;
	TextureType type;
};

// Shader properites for a material
// Will be passed to the shaders using push constant
struct MaterialProperties
{
	glm::vec4 ambient;
	glm::vec4 diffuse;
	glm::vec4 specular;
	float opacity;
};

// Stores info on the materials used in the scene
struct Material
{
	std::string name;
	// Material properties
	MaterialProperties properties;
	// The example only uses a diffuse channel
	Texture diffuse;
	// The material's descriptor contains the material descriptors
	VkDescriptorSet descriptorSet;
	// Pointer to the pipeline used by this material
	VkPipeline *pipeline;
};

// Stores per-mesh Vulkan resources
struct Mesh
{
	// Index of first index in the scene buffer
	uint32_t indexBase;
	uint32_t indexCount;

	// Pointer to the material used by this mesh
	Material *material;
};

class Scene
{
public:
	Scene(VkDevice device, VkQueue queue, ResourceManager *resMan);
	virtual ~Scene();

	void import(const std::string& filePath);
	void render(VkCommandBuffer cmdBuffer);

	// my work
	bool rebindTexture();

	Buffer uniformBuffer;
	struct UniformData {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::vec4 lightPos = glm::vec4(1.25f, 8.35f, 0.0f, 0.0f);
	} uniformData;

	VkPipelineLayout pipelineLayout;

	// Scene uses multiple pipelines
	struct {
		VkPipeline solid;
		VkPipeline blending;
	} pipelines;

private:
	std::string assetPath = "";

	std::vector<Mesh> meshes;
	std::vector<Material> materials;

	//std::set<std::string> loadedTexture;

	VkDevice device;
	VkQueue queue;
	ResourceManager *resMan;

	// We will be using one single index and vertex buffer
	// containing vertices and indices for all meshes in the scene
	// This allows us to keep memory allocations down
	Buffer vertexBuffer;
	Buffer indexBuffer;

	VkSampler defaultSampler;

	VkDescriptorPool descriptorPool;
	// We will be using separate descriptor sets (and bindings)
	// for material and scene related uniforms
	struct
	{
		VkDescriptorSetLayout material;
		VkDescriptorSetLayout scene;
	} descriptorSetLayouts;

	VkDescriptorSet descriptorSetScene;

	void extractMeshes(const aiScene *scene);
	void extractMaterials(const aiScene *scene);

	void loadTextureFromFile(const std::string & fileName, VkFormat format, Texture *texture);
	//Mesh processMesh(aiMesh * aMesh);
	//std::vector<Texture> loadMaterialTextures(aiMaterial *mat, aiTextureType type);
	//Image importTextureFromFile(const std::string& filePath);
	void createSampler(VkSampler *sampler);

};


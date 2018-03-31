#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(set = 0, binding = 0) uniform UniformBufferObject {
	mat4 proj;
    mat4 model;
    mat4 view;
	vec4 lightPos;
} ubo;

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inNormal;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outUV;
layout (location = 3) out vec3 outViewVec;
layout (location = 4) out vec3 outLightVec;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
	outNormal = inNormal;
	outColor = inColor;
	outUV = inUV;

	mat4 modelView = ubo.view * ubo.model;

	gl_Position = ubo.proj * modelView * vec4(inPos.xyz, 1.0);
	
	vec4 pos = modelView * vec4(inPos, 0.0);
	outNormal = mat3(ubo.model) * inNormal;

	vec3 lPos = mat3(ubo.model) * ubo.lightPos.xyz;
	outLightVec = lPos - (ubo.model * vec4(inPos, 1.0)).xyz;
	outViewVec = -(ubo.model * vec4(inPos, 1.0)).xyz;
}
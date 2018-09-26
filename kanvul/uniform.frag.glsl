#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout (std140, binding = 0) uniform bufVals {
	vec4 color;
};

layout (location = 0) out vec4 OC;

void main() {
    OC = color;
}

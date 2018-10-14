#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout(std140, binding = 1) uniform MVP {
	mat4 model;
};

out gl_PerVertex {
    vec4 gl_Position;
};

layout (location = 0) in vec4 pos;

void main() {
    gl_Position = model * pos;
}

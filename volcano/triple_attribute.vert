#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout(std140, binding = 1) uniform MVP {
	mat4 model;
};

out gl_PerVertex {
    vec4 gl_Position;
};

layout (location = 0) in vec4 pos;
layout (location = 1) in vec4 colr;
layout (location = 2) in vec2 coord;

layout (location = 0) out vec4 varying_colr;
layout (location = 1) out vec2 texc;

void main() {
    varying_colr = colr;
    texc = coord;
    gl_Position = model * pos;
}

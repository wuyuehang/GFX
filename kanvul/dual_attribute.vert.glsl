#version 450 core
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex {
    vec4 gl_Position;
};

layout (location = 0) in vec4 pos;
layout (location = 1) in vec4 colr;

layout (location = 0) out vec4 varying_colr;

void main() {
    varying_colr = colr;
    gl_Position = pos;
}

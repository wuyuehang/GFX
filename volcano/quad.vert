#version 450 core
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex {
    vec4 gl_Position;
};

layout (location = 0) in vec4 pos;
layout (location = 1) in vec2 coord;

layout (location = 0) out vec2 texc;

void main() {
    texc = coord;
    gl_Position = pos;
}

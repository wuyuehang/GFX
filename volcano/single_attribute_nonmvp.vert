#version 450 core
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex {
    vec4 gl_Position;
};

layout (location = 0) in vec4 pos;

void main() {
    gl_Position = pos;
}

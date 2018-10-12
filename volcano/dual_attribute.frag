#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec4 varying_colr;
layout (location = 1) in vec2 texc;
layout (location = 0) out vec4 OC;
layout (binding = 0) uniform sampler2D txcsmp;

layout (push_constant) uniform weight {
    float factor;
};

void main() {
    OC = factor * varying_colr + (1 - factor) * texture(txcsmp, texc);
}

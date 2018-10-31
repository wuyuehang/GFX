#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 texc;
layout (location = 0) out vec4 OC;
layout (set = 0, binding = 0) uniform sampler2D tex0;
layout (set = 0, binding = 1) uniform sampler2D tex1;

void main() {
    OC = mix(texture(tex0, texc), texture(tex1, texc), 0.5);
}

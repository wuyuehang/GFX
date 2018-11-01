#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput ipa0;
layout (input_attachment_index = 1, set = 0, binding = 1) uniform subpassInput ipa1;
layout (location = 0) out vec4 OC;

void main() {
    OC = mix(subpassLoad(ipa0), subpassLoad(ipa1), 0.5);
}

#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 texc;
layout (location = 0) out vec4 OC;
layout (set = 0, binding = 0) uniform texture2D tex;
layout (set = 0, binding = 1) uniform sampler smp;
layout (constant_id = 5) const bool graylism = false;
layout (constant_id = 7) const float factor = 0.0;

void main() {
    vec4 temp = texture(sampler2D(tex, smp), texc);

    if (graylism) {
        float intense = (temp.r + temp.g + temp.b) / 3.0 * factor;
        OC = vec4(intense);
    } else {
        OC = temp;
    }
}

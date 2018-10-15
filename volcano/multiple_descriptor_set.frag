#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) out vec4 OC;
layout (set = 0, binding = 0) uniform samplerBuffer texbuf;

void main() {
    int cur_row = int(gl_FragCoord.y - 0.5);
    int cur_col = int(gl_FragCoord.x - 0.5);
    int addr = cur_row * 800 * 4 + cur_col * 4;
    OC.r = texelFetch(texbuf, addr).r;
    OC.g = texelFetch(texbuf, addr + 1).r;
    OC.b = texelFetch(texbuf, addr + 2).r;
    OC.a = texelFetch(texbuf, addr + 3).r;
}

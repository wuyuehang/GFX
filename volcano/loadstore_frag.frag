#version 450 core
#extension GL_ARB_separate_shader_objects : enable

precision highp float;
precision highp int;

layout (location = 0) out vec4 OC;
layout (set = 0, binding = 0, rgba8) uniform readonly highp image2D srcObj;

void main() {
    int y = int(gl_FragCoord.y - 0.5);
    int x = int(gl_FragCoord.x - 0.5);
    OC = imageLoad(srcObj, ivec2(x, y));
}

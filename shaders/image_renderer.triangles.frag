/* SPDX-License-Identifier: MPL-2.0 */
#version 450
layout(binding=0) uniform sampler2D tex[512];
layout(location=0) in vec2 texCoord;
layout(location=1) in vec4 vertexColor;
layout(location=0) out vec4 outColor;
layout(push_constant) uniform TriangleParams {
    layout(offset=0) vec4 positions[3];
    layout(offset=48) vec4 colors[3];
    layout(offset=96) vec2 uvs[3];
    layout(offset=120) uint index;
} params;
void main() { outColor=texture(tex[params.index],texCoord)*vertexColor; }

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
    layout(offset=124) uint encoding;
} params;
vec3 linear_to_srgb(vec3 c) {
    return mix(1.055*pow(max(c,vec3(0)),vec3(1.0/2.4))-0.055,c*12.92,lessThanEqual(c,vec3(0.0031308)));
}

void main() { vec4 c=texture(tex[params.index],texCoord);if(params.encoding!=0u)c.rgb=linear_to_srgb(c.rgb);outColor=c*vertexColor; }

/* SPDX-License-Identifier: MPL-2.0 */
#version 450
layout(push_constant) uniform TriangleEncoding {layout(offset=124) uint encoding;} params;
layout(binding=0) uniform sampler2D tex;
layout(location=0) in vec2 texCoord;
layout(location=1) in vec4 vertexColor;
layout(location=0) out vec4 outColor;
vec3 linear_to_srgb(vec3 c) {
    return mix(1.055*pow(max(c,vec3(0)),vec3(1.0/2.4))-0.055,c*12.92,lessThanEqual(c,vec3(0.0031308)));
}

void main() { vec4 c=texture(tex,texCoord);if(params.encoding!=0u)c.rgb=linear_to_srgb(c.rgb);outColor=c*vertexColor; }

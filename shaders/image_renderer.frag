/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 450

layout(binding = 0) uniform sampler2D tex[512];

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform ImageParams
{
    mat4 matrix;
    vec4 color;
    uint index;
    uint encoding;
} params;

vec3 linear_to_srgb(vec3 c) {
    return mix(1.055*pow(max(c,vec3(0)),vec3(1.0/2.4))-0.055,c*12.92,lessThanEqual(c,vec3(0.0031308)));
}

void main()
{
	vec4 sampleColor=texture(tex[params.index], inTexCoord);
    if(params.encoding!=0u) sampleColor.rgb=linear_to_srgb(sampleColor.rgb);
    outColor=sampleColor*params.color;
}

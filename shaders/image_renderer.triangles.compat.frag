/* SPDX-License-Identifier: MPL-2.0 */
#version 450
layout(binding=0) uniform sampler2D tex;
layout(location=0) in vec2 texCoord;
layout(location=1) in vec4 vertexColor;
layout(location=0) out vec4 outColor;
void main() { outColor=texture(tex,texCoord)*vertexColor; }

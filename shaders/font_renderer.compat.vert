/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec2 inSize;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec4 outColor;

layout(binding = 0, std140) uniform UBO
{
	mat4 matrix;
	vec2 textureSize;
} uni;

void main()
{
	outTexCoord = inTexCoord;
	outColor = inColor;

	gl_Position = uni.matrix * vec4(inPosition, 0.0, 1.0);
}

/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoords;

layout(location = 0) out vec2 outPosition;
layout(location = 1) out flat vec2 outFlatPosition;
layout(location = 2) out vec4 outColor;
layout(location = 3) out vec2 outTexCoords;

void main()
{
	outPosition = inPosition*2.0 - vec2(1.0, 1.0);
	outFlatPosition = outPosition;
	outColor = inColor;
	outTexCoords = inTexCoords;

	gl_Position = vec4(outPosition, 0.0, 1.0);
}

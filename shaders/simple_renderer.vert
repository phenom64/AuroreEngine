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
	outPosition = inPosition;
	outFlatPosition = inPosition;
	outColor = inColor;
	outTexCoords = inTexCoords;

	gl_Position = vec4(outPosition, 0.0, 1.0);
}

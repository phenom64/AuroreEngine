#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in flat vec2 inFlatPosition;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform RenderParams
{
	vec2 blur[4];
} params;

void main()
{
	float xp = smoothstep(params.blur[0][0], params.blur[0][1], inTexCoord.x);
	float xn = smoothstep(params.blur[1][0], params.blur[1][1], 1.0 - inTexCoord.x);
	float yp = smoothstep(params.blur[2][0], params.blur[2][1], inTexCoord.y);
	float yn = smoothstep(params.blur[3][0], params.blur[3][1], 1.0 - inTexCoord.y);
	float f = xp * xn * yp * yn;

	outColor = inColor * f;
}

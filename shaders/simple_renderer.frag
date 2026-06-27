/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in flat vec2 inFlatPosition;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform RenderParams
{
	vec2 blur[4];
	float border_radius[4];
	float aspect_ratio;
} params;

float edge_factor(vec2 interval, float coordinate)
{
	// A zero interval is the renderer's documented/default "no blur" value.
	// GLSL leaves smoothstep(edge, edge, x) undefined, which caused generic
	// triangle geometry (clock hands, rings, focus marks) to disappear on some
	// drivers while rectangles happened to survive on others.
	if(abs(interval.y - interval.x) < 0.000001) {
		return 1.0;
	}
	return smoothstep(interval.x, interval.y, coordinate);
}

void main()
{
	float xp = edge_factor(params.blur[0], inTexCoord.x);
	float xn = edge_factor(params.blur[1], 1.0 - inTexCoord.x);
	float yp = edge_factor(params.blur[2], inTexCoord.y);
	float yn = edge_factor(params.blur[3], 1.0 - inTexCoord.y);
	float f1 = xp * xn * yp * yn;

	float f2 = 1.0;
	if(!isnan(params.aspect_ratio)) {
		vec2 factor = vec2(params.aspect_ratio, 1.0);
		vec2 scaled_position = inTexCoord * factor;
		if(scaled_position.x < params.border_radius[0] && scaled_position.y < params.border_radius[0]) {
			float d = distance(scaled_position, vec2(0.0, 0.0)+vec2(params.border_radius[0]));
			if(d > params.border_radius[0]) {
				f2 = 0.0;
			}
		}
		if(scaled_position.x > factor.x - params.border_radius[1] && scaled_position.y < params.border_radius[1]) {
			float d = distance(scaled_position, vec2(factor.x, 0.0)+vec2(-params.border_radius[1], params.border_radius[1]));
			if(d > params.border_radius[1]) {
				f2 = 0.0;
			}
		}
		if(scaled_position.x > factor.x - params.border_radius[2] && scaled_position.y > factor.y - params.border_radius[2]) {
			float d = distance(scaled_position, factor-vec2(params.border_radius[2]));
			if(d > params.border_radius[2]) {
				f2 = 0.0;
			}
		}
		if(scaled_position.x < params.border_radius[3] && scaled_position.y > factor.y - params.border_radius[3]) {
			float d = distance(scaled_position, vec2(0.0, factor.y)+vec2(params.border_radius[3], -params.border_radius[3]));
			if(d > params.border_radius[3]) {
				f2 = 0.0;
			}
		}
	}

	outColor = inColor * f1 * f2;
}

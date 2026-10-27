/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#version 450

layout(binding = 0, std140) uniform UBO
{
	mat4 matrix;
	vec2 textureSize;
    vec2 _pad;
    vec4 effects;
    vec4 fade_bounds;
    vec4 fade_values;
} uni;
layout(binding = 1) uniform sampler2D tex;

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main()
{
	vec4 coverage = texture(tex, inTexCoord);
    float ink=coverage.r*inColor.a;
    float glow=clamp(coverage.b*uni.effects.y,0.0,1.0)*inColor.a;
    float shadow=texture(tex,inTexCoord-uni.effects.zw).g*uni.effects.x*inColor.a;
    float alpha=ink+(1.0-ink)*(glow+(1.0-glow)*shadow);
    vec3 rgb=(inColor.rgb*ink+inColor.rgb*glow*(1.0-ink))/max(alpha,0.00001);
    if(uni.fade_bounds.y>uni.fade_bounds.x) {
        float x=clamp((gl_FragCoord.x-uni.fade_bounds.x)/(uni.fade_bounds.y-uni.fade_bounds.x),0.0,1.0);
        float mask=x<uni.fade_bounds.z?uni.fade_values.x:x<uni.fade_bounds.w?
            mix(uni.fade_values.x,uni.fade_values.y,(x-uni.fade_bounds.z)/(uni.fade_bounds.w-uni.fade_bounds.z)):
            x<uni.fade_values.w?mix(uni.fade_values.y,uni.fade_values.z,(x-uni.fade_bounds.w)/(uni.fade_values.w-uni.fade_bounds.w)):uni.fade_values.z;
        alpha*=mask;
    }
    outColor=vec4(rgb,alpha);
}

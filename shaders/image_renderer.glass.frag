#version 450

layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConsts {
    mat4 matrix; // unused here, kept for layout parity
    vec4 color;
    uint index;   // unused here for glass variant
} pc;

layout(set = 0, binding = 0) uniform sampler2D uNormalMap;
layout(set = 0, binding = 1) uniform sampler2D uAmbMap;
layout(set = 0, binding = 2) uniform sampler2D uEnvMap;
layout(set = 0, binding = 3) uniform sampler2D uBackgroundMap;

layout(location = 0) in vec2 vUV;

vec3 srgb_to_linear(vec3 c) {return mix(pow((c+0.055)/1.055,vec3(2.4)),c/12.92,lessThanEqual(c,vec3(0.04045)));}
vec3 linear_to_srgb(vec3 c) {return mix(1.055*pow(max(c,vec3(0)),vec3(1.0/2.4))-0.055,c*12.92,lessThanEqual(c,vec3(0.0031308)));}
vec4 frame() {
    vec2 size=1.0/max(vec2(abs(dFdx(vUV.x)),abs(dFdy(vUV.y))),vec2(0.000001));
    vec2 p=vUV*size;float radius=4.0*size.y/58.0;
    float left=p.x;
    if(p.x<radius) {
        if(p.y<radius) left=radius-length(p-vec2(radius));
        else if(p.y>size.y-radius) left=radius-length(p-vec2(radius,size.y-radius));
    }
    float inside=min(left,min(p.y,size.y-p.y));
    float coverage=clamp(inside+0.5,0.0,1.0);
    float border=0.36*(1.0-smoothstep(0.5,1.5,inside));
    float glow=0.22*exp(-pow(max(0.0,inside-1.0)/(2.2*size.y/58.0),2.0));
    vec2 screen=gl_FragCoord.xy/vec2(textureSize(uBackgroundMap,0));
    vec3 bg=linear_to_srgb(texture(uBackgroundMap,screen).rgb);
    float alpha=(1.0-(1.0-border)*(1.0-glow))*pc.color.a;
    vec3 result=mix(bg*(1.0-0.035*pc.color.a),vec3(1),alpha);
    return vec4(srgb_to_linear(result)*coverage,coverage);
}
// RGBA caches body light, white gloss, opacity and silhouette coverage.
// Tint stays dynamic; all bevel, lighting and translucency work is baked offline.
vec4 glassIcon(vec2 uv) {
    vec4 material=texture(uNormalMap,uv);
    if(material.a<1.0/255.0) return vec4(0);
    vec2 screen=gl_FragCoord.xy/vec2(textureSize(uBackgroundMap,0));
    vec3 background=linear_to_srgb(texture(uBackgroundMap,screen).rgb);
    vec3 surface=material.r*pc.color.rgb+material.g;
    float opacity=clamp(max(material.b,max(surface.r,max(surface.g,surface.b))),0.0,1.0);
    float coverage=clamp(material.a*pc.color.a,0.0,1.0);
    vec3 result=background*(1.0-opacity*coverage)+min(surface,vec3(opacity))*coverage;
    return vec4(srgb_to_linear(result),1);
}

void main() {
    FragColor = pc.index==1u?frame():glassIcon(vUV);
}
#version 450

layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConsts {
    mat4 matrix; // unused here, kept for layout parity
    vec4 color;
    uint index;   // unused here for glass variant
} pc;

layout(set = 0, binding = 0) uniform sampler2D uIcon;

layout(location = 0) in vec2 vUV;

vec3 safe_normalize(vec3 value, vec3 fallback)
{
    const float len2 = dot(value, value);
    return len2 > 0.000001 ? value * inversesqrt(len2) : fallback;
}

// xmb-web-style translucent icon glass approximation.  The full browser path
// shades normal maps against ambient/env/refraction textures; this renderer only
// receives the resolved icon image, so we reconstruct a stable pseudo-normal
// from the alpha mask and output premultiplied translucent colour.  The result
// keeps the source icon readable while letting the live XMB background show
// through, matching the glassy silhouette and rim behavior much more closely
// than the previous self-blur pass.
vec4 glassIcon(sampler2D tex, vec2 uv) {
    vec4 base = texture(tex, uv);
    if(base.a <= 0.001) {
        return vec4(0.0);
    }

    vec2 texel = 1.0 / vec2(textureSize(tex, 0));
    float alpha_l = texture(tex, uv - vec2(texel.x, 0.0)).a;
    float alpha_r = texture(tex, uv + vec2(texel.x, 0.0)).a;
    float alpha_u = texture(tex, uv - vec2(0.0, texel.y)).a;
    float alpha_d = texture(tex, uv + vec2(0.0, texel.y)).a;
    vec2 alpha_gradient = vec2(alpha_r - alpha_l, alpha_d - alpha_u);

    vec2 nxy = alpha_gradient * 3.2 + (uv - vec2(0.5)) * 0.18;
    vec3 N = safe_normalize(vec3(-nxy, 1.0), vec3(0.0, 0.0, 1.0));
    vec3 V = safe_normalize(vec3((uv - vec2(0.5)) * -0.30, 1.0),
                            vec3(0.0, 0.0, 1.0));

    // Same day-theme light directions used by xmb-web's icon material.
    const vec3 light1 = normalize(vec3(-0.5358268, 0.3875156, 0.7500925));
    const vec3 light2 = normalize(vec3( 0.7071068, 0.8660254,-0.3535534));
    vec3 H1 = normalize(V + light1);
    vec3 H2 = normalize(V + light2);

    float ndotv = clamp(dot(N, V), 0.0, 1.0);
    float fresnel = pow(1.0 - ndotv, 2.0);
    float edge = smoothstep(0.02, 0.34, length(alpha_gradient));
    float spec1 = pow(max(dot(N, H1), 0.0), 100.0) * 2.4;
    float spec2 = pow(max(dot(N, H2), 0.0), 30.0) * 1.6;

    vec2 refr_dir = N.xy * 0.012;
    vec3 chroma = vec3(
        texture(tex, clamp(uv + refr_dir * -0.80, vec2(0.0), vec2(1.0))).r,
        texture(tex, clamp(uv + refr_dir * -1.00, vec2(0.0), vec2(1.0))).g,
        texture(tex, clamp(uv + refr_dir * -1.25, vec2(0.0), vec2(1.0))).b);

    float luma = dot(base.rgb, vec3(0.299, 0.587, 0.114));
    vec3 icon_detail = mix(vec3(luma), base.rgb, 0.40);
    vec3 ambient = vec3(0.74, 0.80, 0.90) * (0.38 + 0.34 * clamp(N.y * 0.5 + 0.5, 0.0, 1.0));
    vec3 env = vec3(0.82, 0.90, 1.0) *
        (0.16 + 0.54 * pow(clamp(N.z * 0.65 + N.y * 0.35, 0.0, 1.0), 1.6));
    float shadow = (1.0 - max(dot(N, light1), 0.0)) * 0.24;

    vec3 lit = chroma * 0.22
             + icon_detail * 0.32
             + ambient * 0.40
             + env * 0.58
             + vec3(spec1 + spec2) * 1.15
             + vec3(fresnel) * 0.68
             - vec3(0.00, 0.02, 0.05) * shadow;
    vec3 surface = vec3(1.0) - exp(-max(lit, vec3(0.0)) * 0.95);
    surface *= 1.08 * pc.color.rgb;

    float glass_alpha = base.a * (0.52 + 0.36 * fresnel + 0.10 * edge) * pc.color.a;
    glass_alpha = clamp(glass_alpha, 0.0, 0.96);
    return vec4(surface * glass_alpha, glass_alpha);
}

void main() {
    FragColor = glassIcon(uIcon, vUV);
}


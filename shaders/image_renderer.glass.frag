#version 450

layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConsts {
    mat4 matrix; // unused here, kept for layout parity
    vec4 color;
    uint index;   // unused here for glass variant
} pc;

layout(set = 0, binding = 0) uniform sampler2D uNormalMap;
layout(set = 0, binding = 1) uniform sampler2D uBackgroundMap;

layout(location = 0) in vec2 vUV;

vec3 safe_normalize(vec3 value, vec3 fallback)
{
    const float len2 = dot(value, value);
    return len2 > 0.000001 ? value * inversesqrt(len2) : fallback;
}

// xmb-web/PS3-style icon glass material.  This path intentionally consumes the
// imported xmb-web normal-map PNGs (normalmaps/nmap_###.png), not the flat icon
// PNG fallback.  RGB supplies the icon normal and alpha is the silhouette mask.
// The live rendered shell background is sampled behind the icon with chromatic
// offsets so the background visibly bends through the translucent body.
vec4 glassIcon(vec2 uv) {
    vec4 nm = texture(uNormalMap, uv);
    if(nm.a < (1.0 / 255.0)) {
        return vec4(0.0);
    }

    vec2 nxy = nm.rg * 2.0 - 1.0;
    float nz = sqrt(max(0.0, 1.0 - dot(nxy, nxy)));
    vec3 N = safe_normalize(vec3(nxy, nz), vec3(0.0, 0.0, 1.0));
    vec3 V = safe_normalize(vec3((uv - vec2(0.5)) * -0.30, 1.0),
                            vec3(0.0, 0.0, 1.0));

    // Same day-theme light directions used by xmb-web's icon material.
    const vec3 light1 = normalize(vec3(-0.5358268, 0.3875156, 0.7500925));
    const vec3 light2 = normalize(vec3( 0.7071068, 0.8660254,-0.3535534));
    vec3 H1 = normalize(V + light1);
    vec3 H2 = normalize(V + light2);

    float ndotv = clamp(dot(N, V), 0.0, 1.0);
    float fresnel = pow(1.0 - ndotv, 2.0);
    float spec1 = pow(max(dot(N, H1), 0.0), 100.0) * 2.4;
    float spec2 = pow(max(dot(N, H2), 0.0), 30.0) * 1.6;

    vec2 bgSize = vec2(textureSize(uBackgroundMap, 0));
    vec2 screenUV = clamp(gl_FragCoord.xy / max(bgSize, vec2(1.0)), vec2(0.0), vec2(1.0));

    // Derive the icon's screen-UV footprint from derivatives so callers only
    // need to draw a normal-map quad.  This mirrors xmb-web's uBgRadiusUV*2.
    vec2 dUvDx = dFdx(uv);
    vec2 dUvDy = dFdy(uv);
    vec2 pixelUV = 1.0 / max(bgSize, vec2(1.0));
    vec2 iconUVScale = vec2(
        abs(dUvDx.x) > 0.000001 ? abs(pixelUV.x / dUvDx.x) : pixelUV.x,
        abs(dUvDy.y) > 0.000001 ? abs(pixelUV.y / dUvDy.y) : pixelUV.y);
    vec2 refr_dir = nxy * 12.0 * iconUVScale;
    vec3 chroma = vec3(
        texture(uBackgroundMap, clamp(screenUV + refr_dir * -0.096, vec2(0.0), vec2(1.0))).r,
        texture(uBackgroundMap, clamp(screenUV + refr_dir * -0.114, vec2(0.0), vec2(1.0))).g,
        texture(uBackgroundMap, clamp(screenUV + refr_dir * -0.144, vec2(0.0), vec2(1.0))).b);

    vec3 ambient = vec3(0.74, 0.80, 0.90) * (0.5 + 0.5 * clamp(N.y * 0.5 + 0.5, 0.0, 1.0));
    vec3 env = vec3(0.82, 0.90, 1.0) *
        (0.22 + 0.60 * pow(clamp(N.z * 0.65 + N.y * 0.35, 0.0, 1.0), 1.6));
    float shadow = (1.0 - max(dot(N, light1), 0.0)) * 0.30;

    vec3 lit = chroma * 0.35
             + ambient * 0.55
             + env * 2.0 * 0.60
             + vec3(spec1 + spec2) * 1.80
             + vec3(fresnel) * 0.75
             - vec3(0.00, 0.02, 0.05) * shadow;
    vec3 surface = vec3(1.0) - exp(-max(lit, vec3(0.0)) * 0.95);
    surface *= 1.08 * pc.color.rgb;

    float glass_alpha = nm.a * (0.55 + 0.40 * fresnel) * pc.color.a;
    glass_alpha = clamp(glass_alpha, 0.0, 0.96);
    return vec4(surface * glass_alpha, glass_alpha);
}

void main() {
    FragColor = glassIcon(vUV);
}


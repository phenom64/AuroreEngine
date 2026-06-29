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

vec3 safe_normalize(vec3 value, vec3 fallback)
{
    const float len2 = dot(value, value);
    return len2 > 0.000001 ? value * inversesqrt(len2) : fallback;
}

// xmb-web ICON_MATERIAL_DAY + FS_ICON_GLASS (~index.html:2727, :4331).
// Normal-map RGBA from compat/xmb-ui-compat/normalmaps/nmap_###.png.
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

    float ambUVy = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambSample = texture(uAmbMap, vec2(0.5, ambUVy)).rgb;
    vec3 ambient = vec3(0.74, 0.80, 0.90) * (0.5 + 0.5 * ambSample);

    vec2 envUV = N.xy * 0.5 + 0.5;
    vec3 env = texture(uEnvMap, envUV).rgb * 0.74;
    float shadowFactor = (1.0 - max(dot(N, light1), 0.0)) * 0.30;
    vec3 highlights = ambient * 0.55
                    + env * 2.0 * 0.60
                    + vec3(spec1 + spec2) * 1.80
                    + vec3(fresnel) * 0.75
                    - vec3(0.0, 0.02, 0.05) * shadowFactor;

    vec3 totalColor = chroma * 0.35 + highlights;
    vec3 surface = vec3(1.0) - exp(-max(totalColor, vec3(0.0)) * 0.95);
    surface *= 1.08 * pc.color.rgb;

    float fresAlpha = 0.55 + 0.40 * fresnel;
    float glass_alpha = nm.a * fresAlpha * pc.color.a;
    return vec4(surface * glass_alpha, glass_alpha);
}

void main() {
    FragColor = glassIcon(vUV);
}
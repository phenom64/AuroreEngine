#version 450

layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform PushConsts {
    mat4 matrix; // unused here, kept for layout parity
    vec4 color;
    uint index;   // unused here for glass variant
} pc;

layout(set = 0, binding = 0) uniform sampler2D uIcon;

// Convert screen quad coords (NDC) to UV is done in vertex; here we just use gl_FragCoord if needed.
layout(location = 0) in vec2 vUV;

// Simple liquid glass approximation operating only on the icon texture
vec4 liquidGlassIcon(sampler2D tex, vec2 uv) {
    vec4 base = texture(tex, uv);
    float alpha = base.a;
    if(alpha <= 0.001) return vec4(0.0);

    // Radial blur around UV centroid
    vec2 center = vec2(0.5);
    vec2 dir = uv - center;
    float dist = length(dir);
    dir = normalize(dir + 1e-5);

    // Blur strength scales with distance from center (gives glassy edges)
    float strength = smoothstep(0.0, 0.75, dist);
    float radius = mix(0.0015, 0.006, strength);

    vec3 col = base.rgb;
    int samples = 12;
    for(int i = 1; i <= samples; ++i) {
        float t = float(i) / float(samples);
        vec2 off = dir * radius * t;
        col += texture(tex, uv + off).rgb;
    }
    col /= (float(samples) + 1.0);

    // Simple chromatic dispersion
    float disp = 0.003 * strength;
    float r = texture(tex, uv + dir * disp).r;
    float g = texture(tex, uv + dir * disp * 1.5).g;
    float b = texture(tex, uv + dir * disp * 2.0).b;
    vec3 dispersion = vec3(r,g,b);

    vec3 mixed = mix(col, dispersion, 0.6);
    // Apply tint from push constant color and preserve alpha
    return vec4(mixed * pc.color.rgb, alpha * pc.color.a);
}

void main() {
    FragColor = liquidGlassIcon(uIcon, vUV);
}


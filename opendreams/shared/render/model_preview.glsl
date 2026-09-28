@vs vs
layout(binding=0) uniform vs_params {
    mat4 mvp;
    // x: source units per preview unit (1 / frame scale)
    vec4 view_params;
};
in vec3 position;
in vec2 texcoord;
out vec2 uv;
// Camera-space depth in source units: Glide's W (1/oow) for table fog.
out float view_w;
void main() {
    gl_Position = mvp * vec4(position, 1.0);
    uv = texcoord;
    view_w = gl_Position.w * view_params.x;
}
@end

@fs fs
// 8-bit texel indices (R8) and the palette rows of every material
// (256 x materials*32, RGB expanded from each row's RGB565 high word).
@image_sample_type index_tex unfilterable_float
layout(binding=0) uniform texture2D index_tex;
@image_sample_type palette_tex unfilterable_float
layout(binding=1) uniform texture2D palette_tex;
@sampler_type texel_smp nonfiltering
layout(binding=0) uniform sampler texel_smp;
in vec2 uv;
in float view_w;
out vec4 frag_color;
layout(binding=1) uniform fs_params {
    // x chroma key, y output alpha, z palette texture row, w wrap (1) / clamp (0)
    vec4 draw_mode;
    // rgb Glide fog colour, a fog enabled
    vec4 fog_color;
    // x output gamma exponent (1.25 for grGammaCorrectionValue(0.8), 1 = off)
    vec4 output_mode;
    // guFogGenerateExp table, 64 entries in [0, 255]
    vec4 fog_table[16];
};

float fog_entry(int i) {
    vec4 group = fog_table[i >> 2];
    int lane = i & 3;
    return lane == 0 ? group.x : lane == 1 ? group.y : lane == 2 ? group.z : group.w;
}

// Glide W_i = 2^(3 + i/4) / (8 - i%4): 1, 8/7, 4/3, 8/5, then doubling.
float fog_w(int i) {
    int k = i >> 2;
    int j = i & 3;
    return exp2(float(k)) * 8.0 / float(8 - j);
}

float fog_factor(float w) {
    if (w <= 1.0) return fog_entry(0);
    if (w >= fog_w(63)) return fog_entry(63);
    int k = int(floor(log2(w)));
    float m = w / exp2(float(k));
    int j = m < 8.0 / 7.0 ? 0 : m < 8.0 / 6.0 ? 1 : m < 8.0 / 5.0 ? 2 : 3;
    int i = clamp(k * 4 + j, 0, 62);
    float w0 = fog_w(i);
    float w1 = fog_w(i + 1);
    float t = clamp((w - w0) / (w1 - w0), 0.0, 1.0);
    return mix(fog_entry(i), fog_entry(i + 1), t);
}

vec4 texel(vec2 p, vec2 size, int row) {
    vec2 q = draw_mode.w > 0.5 ? mod(p, size) : clamp(p, vec2(0.0), size - 1.0);
    float value = texelFetch(sampler2D(index_tex, texel_smp), ivec2(q), 0).r;
    int index = int(value * 255.0 + 0.5);
    vec3 color = texelFetch(sampler2D(palette_tex, texel_smp), ivec2(index, row), 0).rgb;
    vec3 key = texelFetch(sampler2D(palette_tex, texel_smp), ivec2(0, row), 0).rgb;
    // The 3dfx chroma key compares the looked-up colour with entry 0.
    bool keyed = all(equal(color, key));
    return vec4(color, keyed ? 0.0 : 1.0);
}

void main() {
    vec2 size = vec2(textureSize(sampler2D(index_tex, texel_smp), 0));
    int row = int(draw_mode.z + 0.5);
    // Bilinear filtering after the palette lookup, as the TMU filters P8.
    vec2 p = uv * size - 0.5;
    vec2 base = floor(p);
    vec2 f = p - base;
    vec4 c00 = texel(base, size, row);
    vec4 c10 = texel(base + vec2(1.0, 0.0), size, row);
    vec4 c01 = texel(base + vec2(0.0, 1.0), size, row);
    vec4 c11 = texel(base + vec2(1.0, 1.0), size, row);
    vec4 sampled = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
    if (draw_mode.x > 0.5 && sampled.a < 0.5) discard;
    vec3 color = sampled.rgb;
    if (fog_color.a > 0.5)
        color = mix(color, fog_color.rgb, fog_factor(view_w) / 255.0);
    color = pow(max(color, vec3(0.0)), vec3(output_mode.x));
    frag_color = vec4(color, draw_mode.y);
}
@end

@program model_preview vs fs

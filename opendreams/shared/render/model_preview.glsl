@vs vs
layout(binding=0) uniform vs_params {
    mat4 mvp;
};
in vec3 position;
in vec2 texcoord;
out vec2 uv;
void main() {
    gl_Position = mvp * vec4(position, 1.0);
    uv = texcoord;
}
@end

@fs fs
layout(binding=0) uniform texture2D image_tex;
layout(binding=0) uniform sampler image_smp;
in vec2 uv;
out vec4 frag_color;
layout(binding=1) uniform fs_params {
    vec4 draw_mode;
};
void main() {
    vec4 sampled = texture(sampler2D(image_tex, image_smp), uv);
    if (draw_mode.x > 0.5 && sampled.a < 0.5) discard;
    frag_color = vec4(sampled.rgb, draw_mode.y);
}
@end

@program model_preview vs fs

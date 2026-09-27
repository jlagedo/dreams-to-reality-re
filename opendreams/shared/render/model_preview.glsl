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
void main() {
    frag_color = texture(sampler2D(image_tex, image_smp), uv);
}
@end

@program model_preview vs fs

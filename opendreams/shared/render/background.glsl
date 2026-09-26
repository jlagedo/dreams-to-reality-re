@vs vs
in vec2 position;
out vec2 uv;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    uv = position * 0.5 + 0.5;
}
@end

@fs fs
in vec2 uv;
out vec4 frag_color;
void main() {
    vec2 p = clamp(uv, 0.0, 1.0);
    vec3 low = vec3(0.035, 0.055, 0.11);
    vec3 high = vec3(0.12, 0.19, 0.31);
    frag_color = vec4(mix(low, high, p.y), 1.0);
}
@end

@program background vs fs

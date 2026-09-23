layout(binding = 5) uniform sampler2D image;
layout(std430, binding = 3) readonly buffer Exposure { uint bins[128]; float ev; };

in vec2 ndc;
layout(location = 0) out vec4 color;

void main() {
    vec3 c = texture(image, ndc * 0.5 + 0.5).rgb * exp2(ev);
    color = vec4(srgb(clamp(light.w > 0.5 ? c.rrr : c, 0.0, 1.0)), 1.0);
}

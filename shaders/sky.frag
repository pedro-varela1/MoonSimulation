in vec3 flux;
layout(location = 0) out vec4 color;

void main() {
    vec2 q = (gl_PointCoord - 0.5) * PSF_SIZE;
    vec3 L = flux * exp(-0.5 * dot(q, q) / (PSF_SIGMA * PSF_SIGMA));
    color = vec4(display(light.w > 0.5 ? L.rrr : L), 0.0);
}

in vec3 col;
flat in int kind;
layout(location = 0) out vec4 color;

void main() {
    if (kind == 1 && length(gl_PointCoord - 0.5) > 0.5) discard;
    color = vec4(col, 1.0);
}

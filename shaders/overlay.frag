in vec3 col;
flat in int kind;
layout(location = 0) out vec4 color;

void main() {
    if (kind == 1 && length(gl_PointCoord - 0.5) > 0.5) discard;
    color = vec4(col, kind == 3 ? 0.14 : 1.0);  // the camera field of view is translucent
}

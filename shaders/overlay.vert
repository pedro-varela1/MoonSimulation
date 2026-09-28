layout(std430, binding = 2) readonly buffer Probe { float vis, above; };
layout(std430, binding = 4) readonly buffer Verts { vec4 v[]; };
layout(location = 0) uniform mat4 viewProj;
layout(location = 1) uniform mat4 model;
layout(location = 2) uniform int mode;  // 0 track, 1 marker, 2 mesh, 3 camera field of view
layout(location = 3) uniform int now;

out vec3 col;
flat out int kind;

void main() {
    kind = mode;
    if (mode == 2) {
        vec3 n = normalize(mat3(model) * v[3 * gl_VertexID + 1].xyz);
        col = v[3 * gl_VertexID + 2].rgb * (0.04 + max(dot(n, sunDir.xyz), 0.0) * vis);
        gl_Position = viewProj * model * vec4(v[3 * gl_VertexID].xyz, 1.0);
        return;
    }
    if (mode == 3) {  // pulled toward the eye along the view ray: same pixel, but the terrain relief does not hide it
        vec3 p = v[gl_VertexID].xyz;
        col = vec3(0.95, 0.76, 0.19);
        gl_Position = viewProj * vec4(p - normalize(p) * min(HMAX + 1.0, 0.5 * length(p)), 1.0);
        return;
    }
    int i = mode == 1 ? now : gl_VertexID;
    col = mode == 1 ? mix(vec3(0.3), vec3(1.0, 0.95, 0.4), vis) : i < now ? vec3(1.0, 0.55, 0.1) : vec3(0.2, 0.8, 1.0);
    gl_PointSize = 9.0;
    gl_Position = viewProj * vec4(v[i].xyz, 1.0);
}

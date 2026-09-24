layout(std430, binding = 5) readonly buffer Sky { vec4 src[]; };
layout(location = 0) uniform mat3 toMe;

out vec3 flux;

// point source at infinity (depth 0), spread by a Gaussian PSF: in the camera its flux over the solid angle of the pixel
// it falls on (physical); in the illustrative view a fixed scale (size.z)
void main() {
    vec4 p = src[2 * gl_VertexID];
    vec3 d = normalize(toMe * p.xyz - p.w * eye.xyz);
    float z = dot(d, fwd.xyz);
    flux = src[2 * gl_VertexID + 1].rgb * (size.z > 0.0 ? size.z : size.x * size.y / (8.0 * PI * PSF_SIGMA * PSF_SIGMA * right.w * up.w * z * z * z));
    gl_Position = z > 0.0 ? vec4(dot(d, right.xyz) / (z * right.w), dot(d, up.xyz) / (z * up.w), 0.0, 1.0) : vec4(0.0, 0.0, -1.0, 1.0);
    gl_PointSize = PSF_SIZE;
}

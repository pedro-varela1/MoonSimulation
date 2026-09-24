layout(std140, binding = 1) uniform View { vec4 eye, right, up, fwd, size; };

in vec2 ndc;
layout(location = 0) out vec4 color;

vec2 texGrad(vec3 dp, vec3 n, vec3 east, vec3 north, float r) {
    return vec2(dot(dp, east) / (2.0 * PI * r * max(length(n.xy), 1e-4)), -dot(dp, north) / (PI * r));
}

void main() {
    vec3 D = fwd.xyz + ndc.x * right.w * right.xyz + ndc.y * up.w * up.xyz, d = normalize(D);
    vec3 dx = 2.0 * right.w / size.x * right.xyz, dy = 2.0 * up.w / size.y * up.xyz;
    dx = (dx - d * dot(d, dx)) / length(D);
    dy = (dy - d * dot(d, dy)) / length(D);
    float t, dA = max(length(dx), length(dy));
    if (!castRay(eye.xyz, d, dA, t)) { color = vec4(0.0); gl_FragDepth = 0.0; return; }

    vec3 p = eye.xyz + t * d, n0 = normalize(p);
    float fp = t * dA, r = length(p), dn = dot(d, n0);
    vec3 N = normalAt(n0, log2(fp / TEXEL));
    vec3 east = length(n0.xy) > 1e-6 ? normalize(vec3(-n0.y, n0.x, 0.0)) : vec3(0, 1, 0), north = cross(n0, east);
    vec3 IF = albedoIF(n0, texGrad(t * (dx - d * dot(dx, n0) / dn), n0, east, north, r),
                           texGrad(t * (dy - d * dot(dy, n0) / dn), n0, east, north, r), fp);
    Hapke P = hapkeAt(n0);
    float mu = max(dot(N, -d), 1e-3), mu0 = dot(N, sunDir.xyz), mu0E = dot(N, earthDir.xyz);
    vec3 L = vec3(0.0);
    float v = mu0 > 0.0 ? visibility(p, sunDir.xyz, sunDir.w, fp) : 0.0;
    if (v > 0.0) L = IF * hapkeRatio(mu0, mu, dot(sunDir.xyz, -d), P) * v;
    else if (mu0E > 0.0 && light.y > 0.0) L = IF * hapkeRatio(mu0E, mu, dot(earthDir.xyz, -d), P) * visibility(p, earthDir.xyz, earthDir.w, fp) * light.y;
    L = (light.w > 0.5 ? L.rrr : L) * light.x;
    color = vec4(eye.w > 0.0 ? srgb(clamp(eye.w * L, 0.0, 1.0)) : L, 1.0);
    gl_FragDepth = fwd.w / (t * dot(d, fwd.xyz));
}

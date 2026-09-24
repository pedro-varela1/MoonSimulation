const float RE = 6371.0, RS = 695700.0, SUN_L = 46238.9;  // SUN_L: mean solar disc radiance in I/F units, (AU / RS)^2
const float EARTH_SCALE = 0.862;  // Blue Marble mean linear reflectance 0.355 -> the 0.306 albedo used for earthshine
const vec3 LIMB = vec3(0.56, 0.63, 0.79), TAU_R = vec3(0.0516, 0.0865, 0.309), GLOW = vec3(1.0, 0.85, 0.6);  // per band 643/566/415 nm

in vec2 ndc;
layout(location = 0) out vec4 color;

vec2 texGrad(vec3 dp, vec3 n, vec3 east, vec3 north, float r) {
    return vec2(dot(dp, east) / (2.0 * PI * r * max(length(n.xy), 1e-4)), -dot(dp, north) / (PI * r));
}

vec3 terrain(vec3 d, vec3 dx, vec3 dy, float t, float dA) {
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
    return L * light.x;
}

// Earth: Blue Marble reflectance (Lambert) seen through a single-scattering Rayleigh layer
vec3 earthIF(vec3 n, vec3 v, vec3 sd, float lod) {
    float mu0 = dot(n, sd), mu = max(dot(n, v), 1e-3), cg = dot(sd, v);
    if (mu0 <= 0.0) return vec3(0.0);
    vec3 nb = n * earthToMe;
    vec3 A = EARTH_SCALE * textureLod(earthTex, vec2(atan(nb.y, nb.x) / (2.0 * PI) + 0.5, 0.5 - asin(nb.z) / PI), lod).rgb;
    vec3 att = exp(-TAU_R * (1.0 / mu + 1.0 / mu0));
    return A * mu0 * att + 0.1875 * (1.0 + cg * cg) * mu0 / (mu + mu0) * (1.0 - att);
}

// Sun (limb darkened; glow only in the illustrative view) and Earth in front, edges antialiased over the pixel footprint
vec3 discs(vec3 d, float dA, out float t) {
    vec3 S = sunDir.xyz * (RS / sin(sunDir.w)), E = earthDir.xyz * (RE / sin(earthDir.w)), s = S - eye.xyz, e = E - eye.xyz;
    float ds = length(s), de = length(e), be = dot(d, e), pe = length(cross(d, e));
    float rs = asin(RS / ds), x = length(cross(d, s)) / ds / rs, front = step(0.0, dot(d, s));
    float cs = front * clamp((1.0 - x) * rs / dA + 0.5, 0.0, 1.0), ce = be > 0.0 ? clamp((asin(RE / de) - pe / de) / dA + 0.5, 0.0, 1.0) : 0.0;
    vec3 ld = 1.0 - LIMB * (1.0 - sqrt(max(1.0 - x * x, 0.0)));
    float g = max(x - 1.0, 0.0);
    vec3 sun = eye.w > 0.0 ? (ld * cs + GLOW * front * (1.0 - cs) * (0.35 * exp(-2.0 * g) + 0.12 * exp(-0.35 * g))) / eye.w
                           : SUN_L * ld / (1.0 - LIMB / 3.0) * cs;
    vec3 n = normalize((be - sqrt(max(RE * RE - pe * pe, 0.0))) * d - e);
    float lod = log2(dA * de / max(dot(n, -d), 0.1) * float(textureSize(earthTex, 0).x) / (2.0 * PI * RE));
    t = ce > 0.0 ? de : cs > 0.0 ? ds : 0.0;
    return ce > 0.0 ? mix(sun, earthIF(n, -d, normalize(S - E), lod) * light.x, ce) : sun;
}

void main() {
    vec3 D = fwd.xyz + ndc.x * right.w * right.xyz + ndc.y * up.w * up.xyz, d = normalize(D);
    vec3 dx = 2.0 * right.w / size.x * right.xyz, dy = 2.0 * up.w / size.y * up.xyz;
    dx = (dx - d * dot(d, dx)) / length(D);
    dy = (dy - d * dot(d, dy)) / length(D);
    float t, dA = max(length(dx), length(dy));
    bool hit = castRay(eye.xyz, d, dA, t);
    vec3 L = hit ? terrain(d, dx, dy, t, dA) : discs(d, dA, t);
    color = vec4(display(light.w > 0.5 ? L.rrr : L), hit ? 1.0 : 0.0);
    gl_FragDepth = t > 0.0 ? fwd.w / (t * dot(d, fwd.xyz)) : 0.0;
}

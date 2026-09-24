const float R = 1737.4, HMAX = 11.0, HMIN = -9.2, HSCALE = 12.0, PI = 3.14159265, SLOPE = 0.8391, TEXEL = 0.0592253;
const float CAP_RHO = radians(16.0), IF_SCALE = 0.25, PC = 15520.0, PS = 0.06, PSF_SIGMA = 0.6, PSF_SIZE = 5.0;

layout(std140, binding = 0) uniform Frame {
    vec4 sunDir, earthDir, light, sldemWin, polarWin;
    mat3 earthToMe;
};
layout(std140, binding = 1) uniform View { vec4 eye, right, up, fwd, size; };
layout(binding = 0) uniform sampler2D lolaTex;
layout(binding = 1) uniform sampler2D albedoTex;
layout(binding = 2) uniform sampler2DArray hapkeTex;
layout(binding = 3) uniform sampler2D sldemTex;
layout(binding = 4) uniform sampler2D sldemCover;
layout(binding = 6) uniform sampler2D capHTex;
layout(binding = 7) uniform sampler2D capATex;
layout(binding = 8) uniform sampler2D polarTex;
layout(binding = 9) uniform sampler2D polarCover;
layout(binding = 10) uniform sampler2D earthTex;

vec2 lonLat(vec3 n) { return vec2(atan(n.y, n.x), asin(clamp(n.z, -1.0, 1.0))); }
vec2 kitUV(vec2 ll) { return vec2(ll.x / (2.0 * PI) + 0.5, 0.5 - ll.y / PI); }
float capWeight(vec2 ll) { return smoothstep(radians(75.0), radians(77.0), abs(ll.y)); }
vec2 capUV(vec3 n) {
    vec2 q = (length(n.xy) > 1e-9 ? normalize(n.xy) : vec2(1, 0)) * acos(abs(n.z)) / CAP_RHO;
    return vec2((q.x * 0.5 + 0.5 + (n.z < 0.0 ? 1.0 : 0.0)) * 0.5, q.y * 0.5 + 0.5);
}
vec3 srgb(vec3 c) { return mix(12.92 * c, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c)); }
vec3 display(vec3 L) { return eye.w > 0.0 ? srgb(clamp(eye.w * L, 0.0, 1.0)) : L; }

float sampleH(sampler2D tex, vec2 uv, float lod) {
    if (lod > 0.5) return textureLod(tex, uv, lod).r;
    vec2 size = vec2(textureSize(tex, 0)), p = uv * size - 0.5, f = fract(p), i = p - f, f2 = f * f, f3 = f2 * f;
    vec2 w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0, w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0, w3 = f3 / 6.0;
    vec2 g0 = w0 + w1, g1 = 1.0 - g0, h0 = (i - 0.5 + w1 / g0) / size, h1 = (i + 1.5 + w3 / g1) / size;
    return g0.y * (g0.x * textureLod(tex, h0, 0.0).r + g1.x * textureLod(tex, vec2(h1.x, h0.y), 0.0).r) +
           g1.y * (g0.x * textureLod(tex, vec2(h0.x, h1.y), 0.0).r + g1.x * textureLod(tex, h1, 0.0).r);
}

// streamed toroidal window: X in source texels, w = coverage weight
float windowH(sampler2D tex, sampler2D cover, vec2 X, vec4 win, vec2 tiles, float lod, out float w) {
    vec2 tile = X / 256.0 - win.xy;
    w = 0.0;
    if (win.z <= 0.0 || lod >= 3.5 || any(lessThan(tile, vec2(0.0))) || any(greaterThanEqual(tile, tiles))) return 0.0;
    w = textureLod(cover, X / 256.0 / tiles, 0.0).r * (1.0 - smoothstep(2.5, 3.5, lod));
    return w > 0.0 ? sampleH(tex, X / (tiles * 256.0), min(lod, 3.0)) * HSCALE : 0.0;
}

// SLDEM 512 ppd, then LOLA 60 m polar stereographic, then LOLA 64 ppd (azimuthal cap grid above 75 deg)
float heightAt(vec3 n, float lod, out float w) {
    vec2 ll = lonLat(n), d = degrees(ll);
    float c = sldemWin.x * 0.5 + 16.0, p = polarWin.w > 0.5 ? -1.0 : 1.0, wp = 0.0, t = 2.0 * R / (1.0 + p * n.z);
    float h = windowH(sldemTex, sldemCover, vec2(c + mod(d.x - c + 180.0, 360.0) - 180.0, 60.0 - d.y) * 512.0, sldemWin, vec2(64.0, 32.0), lod, w);
    if (w < 1.0) {
        float hp = p * n.z > 0.5 ? windowH(polarTex, polarCover, PC + vec2(n.y, p * n.x) * t / PS, polarWin, vec2(32.0), lod, wp) : 0.0;
        float s = capWeight(ll), g = 0.0;
        if (wp < 1.0) {
            g = s > 0.0 ? sampleH(capHTex, capUV(n), lod - 3.0) : 0.0;
            if (s < 1.0) g = mix(sampleH(lolaTex, kitUV(ll), lod - 3.0), g, s);
        }
        h = mix(mix(g * HSCALE, hp, wp), h, w);
        w += (1.0 - w) * wp;
    }
    return h;
}

float heightAt(vec3 n, float lod) { float w; return heightAt(n, lod, w); }

vec3 normalAt(vec3 n0, float lod) {
    float w;
    heightAt(n0, lod, w);
    float e = max(max(TEXEL * exp2(max(lod, 0.0)), 0.03), (1.0 - w) * 0.24) / R;
    vec3 t1 = normalize(cross(n0, abs(n0.z) < 0.9 ? vec3(0, 0, 1) : vec3(1, 0, 0))), t2 = cross(n0, t1);
    float dx = heightAt(normalize(n0 + e * t1), lod) - heightAt(normalize(n0 - e * t1), lod);
    float dy = heightAt(normalize(n0 + e * t2), lod) - heightAt(normalize(n0 - e * t2), lod);
    return normalize(n0 - (dx * t1 + dy * t2) / (2.0 * e * R));
}

bool castRay(vec3 o, vec3 d, float dA, out float t) {
    float b = dot(o, d), disc = b * b - dot(o, o) + (R + HMAX) * (R + HMAX);
    t = 0.0;
    if (disc < 0.0 || -b + sqrt(disc) < 0.0) return false;
    float t1 = -b + sqrt(disc), tp = max(-b - sqrt(disc), 0.0), gp = 1e9;
    t = tp;
    for (int i = 0; i < 200; i++) {
        vec3 q = o + t * d;
        float r = length(q), lod = log2(max(t * dA, 1e-6) / TEXEL);
        float g = r - R - heightAt(q / r, lod);
        if (g < 0.0) {
            for (int k = 0; k < 5; k++) {
                float tm = tp + (t - tp) * gp / (gp - g);
                vec3 qm = o + tm * d;
                float rm = length(qm), gm = rm - R - heightAt(qm / rm, lod);
                if (gm < 0.0) { t = tm; g = gm; } else { tp = tm; gp = gm; }
            }
            t = tp + (t - tp) * gp / (gp - g);
            return true;
        }
        if (t > t1) return false;
        float sb = -dot(d, q / r);
        tp = t; gp = g;
        t += max(g / max(sb + SLOPE * sqrt(max(1.0 - sb * sb, 0.0)), 0.05), 0.5 * t * dA);
    }
    return true;
}

// fraction of a light disc of angular radius alpha above the terrain horizon (cone-traced march)
float visibility(vec3 p, vec3 L, float alpha, float fp) {
    vec3 n = normalize(p);
    float a = length(p) - R, el = asin(dot(L, n));
    if (el < -acos(clamp((R + HMIN) / (R + max(a, HMIN)), 0.0, 1.0)) - alpha) return 0.0;
    if (el > atan(SLOPE) + alpha) return 1.0;
    float x = 1.0, t = 2.0 * fp;
    for (int i = 0; i < 128; i++) {
        vec3 q = p + t * L;
        float r = length(q);
        if (r > R + HMAX) break;
        vec3 nq = q / r;
        float c = max(fp, t * alpha), sb = dot(L, nq), ch = sqrt(max(1.0 - sb * sb, 0.0));
        float g = r - R - heightAt(nq, log2(c / TEXEL)) + 0.25 * fp;
        x = min(x, g * ch / (t * alpha));
        if (x <= -1.0) break;
        t += clamp(g / max(SLOPE * ch - sb, 0.02), 0.5 * c, 50.0);
    }
    x = clamp(x, -1.0, 1.0);
    return 0.5 + (x * sqrt(1.0 - x * x) + asin(x)) / PI;
}

vec3 albedoIF(vec3 n, vec2 gx, vec2 gy, float fp) {
    vec2 ll = lonLat(n);
    float s = capWeight(ll);
    vec3 dn = s < 1.0 ? textureGrad(albedoTex, kitUV(ll), gx, gy).rgb : vec3(0.0);
    if (s > 0.0) dn = mix(dn, textureLod(capATex, capUV(n), log2(fp / 0.474)).rgb, s);
    return dn * IF_SCALE;
}

struct Hapke { vec3 w, b, c, bs0, hs; };

Hapke hapkeAt(vec3 n) {
    vec2 d = degrees(lonLat(n)), uv = vec2(mod(d.x, 360.0) / 360.0, (71.0 - d.y) / 142.0);
    return Hapke(textureLod(hapkeTex, vec3(uv, 0), 0.0).rgb, textureLod(hapkeTex, vec3(uv, 1), 0.0).rgb,
                 textureLod(hapkeTex, vec3(uv, 2), 0.0).rgb, textureLod(hapkeTex, vec3(uv, 3), 0.0).rgb,
                 textureLod(hapkeTex, vec3(uv, 4), 0.0).rgb);
}

float E1(float x, float ct) { return exp(-0.636620 * ct / tan(x)); }
float E2(float x, float ct) { float y = ct / tan(x); return exp(-0.318310 * y * y); }
float eta(float x, float tt, float ct, float chi) { return chi * (cos(x) + sin(x) * tt * E2(x, ct) / (2.0 - E1(x, ct))); }
vec3 H(float x, vec3 w) { vec3 g = sqrt(1.0 - w), r0 = (1.0 - g) / (1.0 + g); return 1.0 / (1.0 - w * x * (r0 + (1.0 - 2.0 * r0 * x) * 0.5 * log((1.0 + x) / x))); }
vec3 phase(float cg, vec3 b, vec3 c) {
    vec3 b2 = b * b;
    return 0.5 * (1.0 + c) * (1.0 - b2) / pow(1.0 - 2.0 * b * cg + b2, vec3(1.5)) + 0.5 * (1.0 - c) * (1.0 - b2) / pow(1.0 + 2.0 * b * cg + b2, vec3(1.5));
}

// Hapke 2012 with Sato et al. 2014 maps: r(i,e,g) / r(60,0,60), the mosaic normalization geometry
vec3 hapkeRatio(float mu0, float mu, float cg, Hapke P) {
    float tt = light.z, ct = 1.0 / tt, chi = inversesqrt(1.0 + PI * tt * tt);
    float i = clamp(acos(mu0), 1e-3, PI / 2.0 - 1e-3), e = clamp(acos(mu), 1e-3, PI / 2.0 - 1e-3), g = acos(clamp(cg, -1.0, 1.0));
    float cp = clamp((cg - cos(i) * cos(e)) / (sin(i) * sin(e)), -1.0, 1.0), psi = min(acos(cp), PI - 1e-4);
    float f = exp(-2.0 * tan(0.5 * psi)), s2 = sin(0.5 * psi) * sin(0.5 * psi);
    float ei = eta(i, tt, ct, chi), ee = eta(e, tt, ct, chi), mu0e, mue, S;
    if (i <= e) {
        float D = 2.0 - E1(e, ct) - psi / PI * E1(i, ct);
        mu0e = chi * (cos(i) + sin(i) * tt * (cp * E2(e, ct) + s2 * E2(i, ct)) / D);
        mue = chi * (cos(e) + sin(e) * tt * (E2(e, ct) - s2 * E2(i, ct)) / D);
        S = mue / ee * cos(i) / ei * chi / (1.0 - f + f * chi * cos(i) / ei);
    } else {
        float D = 2.0 - E1(i, ct) - psi / PI * E1(e, ct);
        mu0e = chi * (cos(i) + sin(i) * tt * (E2(i, ct) - s2 * E2(e, ct)) / D);
        mue = chi * (cos(e) + sin(e) * tt * (cp * E2(i, ct) + s2 * E2(e, ct)) / D);
        S = mue / ee * cos(i) / ei * chi / (1.0 - f + f * chi * cos(e) / ee);
    }
    mu0e = max(mu0e, 1e-4);
    mue = max(mue, 1e-4);
    vec3 r = mu0e / (mu0e + mue) * (phase(cg, P.b, P.c) * (1.0 + P.bs0 / (1.0 + tan(0.5 * g) / P.hs)) + H(mu0e, P.w) * H(mue, P.w) - 1.0) * S;
    float e60 = eta(radians(60.0), tt, ct, chi);
    vec3 r60 = 0.5 * chi / (e60 + chi) * (phase(0.5, P.b, P.c) * (1.0 + P.bs0 / (1.0 + 0.577350 / P.hs)) + H(e60, P.w) * H(chi, P.w) - 1.0);
    return max(r / r60, 0.0);
}

#include "stages.h"
#include <algorithm>
#include <map>

namespace {

// an STK orbit (MOON_ME) with its ascending nodes; times in seconds from the file's first row
struct Track {
    const OrbitSpec* spec;
    Orbit orbit;
    double et0 = 0;
    std::vector<double> nodes;
    int rev(double t) const { return int(std::upper_bound(nodes.begin(), nodes.end(), t) - nodes.begin()); }
    glm::dmat3 lvlh(const State& s) const {  // pointing() base: right of the track, along the track, up
        glm::dvec3 U = glm::normalize(s.p), F = glm::normalize(s.v - glm::dot(s.v, U) * U);
        return {glm::cross(F, U), F, U};
    }
};

const Track& track(const Settings& S, size_t i) {
    static std::map<size_t, Track> cache;
    if (auto it = cache.find(i); it != cache.end()) return it->second;
    Track& k = cache[i];
    k.spec = &S.orbits[i];
    k.orbit.load(k.spec->file, glm::dmat3(1));
    k.et0 = ephem::et(k.orbit.utc0);
    const std::vector<double>& t = k.orbit.times();
    for (size_t j = 0; j + 1 < t.size(); j++) {
        double z0 = k.orbit.at(t[j]).p.z, z1 = k.orbit.at(t[j + 1]).p.z;
        if (z0 < 0 && z1 >= 0) k.nodes.push_back(t[j] + (t[j + 1] - t[j]) * -z0 / (z1 - z0));
    }
    return k;
}

double shiftSeconds(const Settings& S, int k) { return k * S.siderealDays * 86400; }

// truth columns of a row: time, state in MOON_ME and in the Moon-centred J2000 frame
std::string truthRow(const Track& k, double t, glm::dvec3* hMe = nullptr) {
    State s = k.orbit.at(t);
    double et = k.et0 + t;
    glm::dmat3 R, dR;
    ephem::stateRotation("MOON_ME", "J2000", et, R, dR);
    glm::dvec3 rm = R * s.p, vm = R * s.v + dR * s.p;
    if (hMe) *hMe = glm::normalize(glm::transpose(R) * glm::cross(rm, vm));  // orbit normal (inertial angular momentum) in MOON_ME
    glm::dvec2 l = latLon(s.p);
    return fmt("%.3f,%s,%.6f,%.8f,%.8f,%.6f,%.6f,%.6f,%.6f,%.9f,%.9f,%.9f,%.6f,%.6f,%.6f,%.9f,%.9f,%.9f", t, k.orbit.utc(t).c_str(), et, l.x, l.y,
               glm::length(s.p) - MOON_R, s.p.x, s.p.y, s.p.z, s.v.x, s.v.y, s.v.z, rm.x, rm.y, rm.z, vm.x, vm.y, vm.z);
}
const char* TRUTH = "t_s,utc,et,lat_deg,lon_deg,h_km,r_me_x,r_me_y,r_me_z,v_me_x,v_me_y,v_me_z,r_mci_x,r_mci_y,r_mci_z,v_mci_x,v_mci_y,v_mci_z";

// Sun direction (MOON_ME) at the shifted epoch, from samples every 60 s, interpolated
struct SunPath {
    double t0;
    std::vector<glm::dvec3> s;
    SunPath(const Track& k, double t0, double t1, double shift) : t0(t0) {
        for (double t = t0; t <= t1 + 60; t += 60) s.push_back(glm::normalize(ephem::sun(k.et0 + t + shift)));
    }
    glm::dvec3 at(double t) const {
        double x = (t - t0) / 60;
        size_t i = std::min(size_t(x), s.size() - 2);
        return glm::normalize(glm::mix(s[i], s[i + 1], x - double(i)));
    }
};

}

void truthStage(const Settings& S) {
    for (size_t i = 0; i < S.orbits.size(); i++) {
        const Track& k = track(S, i);
        const std::string& name = k.spec->name;
        std::string dir = S.root + "/S2/truth/";
        printf("truth %s\n", name.c_str()), fflush(stdout);
        const std::vector<double>& t = k.orbit.times();
        std::string text = std::string(TRUTH) + "\n";
        text.reserve(t.size() * 260);
        for (double x : t) text += truthRow(k, x) + "\n";
        writeText(dir + "truth_" + name + ".csv", text);
        for (int e : S.epochs) {
            SunPath sun(k, t.front(), t.back(), shiftSeconds(S, e));
            std::string beta = "t_s,utc,beta_deg,sun_el_ss_deg\n", arcs = "start_t_s,start_utc,end_t_s,end_utc,duration_min,complete\n";
            double start = NAN;
            bool dark = false;
            for (size_t j = 0; j < t.size(); j++) {
                glm::dvec3 p = glm::normalize(k.orbit.at(t[j]).p), s = sun.at(t[j]);
                bool d = glm::dot(p, s) < 0;
                if (j == 0) dark = d, start = d ? t[0] : NAN;
                else if (d != dark) {
                    if (d) start = t[j];
                    else arcs += fmt("%.3f,%s,%.3f,%s,%.4f,%d\n", start, k.orbit.utc(start).c_str(), t[j], k.orbit.utc(t[j]).c_str(), (t[j] - start) / 60, int(start > t[0]));
                    dark = d;
                }
                if (std::fmod(t[j] - t[0], 60) < 1e-6) {
                    glm::dvec3 h(0);
                    truthRow(k, t[j], &h);
                    beta += fmt("%.3f,%s,%.6f,%.6f\n", t[j], k.orbit.utc(t[j]).c_str(), glm::degrees(std::asin(glm::dot(h, s))), glm::degrees(std::asin(glm::dot(p, s))));
                }
            }
            if (dark) arcs += fmt("%.3f,%s,%.3f,%s,%.4f,0\n", start, k.orbit.utc(start).c_str(), t.back(), k.orbit.utc(t.back()).c_str(), (t.back() - start) / 60);
            writeText(dir + fmt("sun_%s_k%d.csv", name.c_str(), e), beta);
            writeText(dir + fmt("arcs_%s_k%d.csv", name.c_str(), e), arcs);
        }
    }
}

std::vector<Shot> clipShots(Generator& g, size_t oi) {
    const Settings& S = g.set;
    const Track& k = track(S, oi);
    const OrbitSpec& o = *k.spec;
    Rng rng(S.clipSeed + oi);
    double reserve = (S.guardHours + S.valHours + S.testHours) * 3600;
    struct Clip { std::string id; int k, fov; double fovDeg, t0, step, tilt, az; };
    std::vector<Clip> clips;
    for (int e : S.epochs)
        for (size_t f = 0; f < o.fovs.size(); f++)
            for (int c = 0; c < S.clipsPerEpoch / int(o.fovs.size()); c++) {
                Clip cl{fmt("c%04zu", clips.size()), e, o.fovs[f], 0, 0, o.steps[f]};
                for (bool ok = false; !ok;) {
                    cl.t0 = std::round(rng(k.orbit.begin(), k.orbit.end() - reserve - S.clipFrames * cl.step));
                    if (cl.fovDeg == 0) cl.fovDeg = cl.fov * rng(1 - S.fovJitter, 1 + S.fovJitter);
                    for (int tries = 0; tries < 100 && !ok; tries++) {  // attitude redrawn until every frame is filled, then the start
                        cl.tilt = rng(0, S.tiltMax), cl.az = rng(0, 360), ok = true;
                        for (int j = 0; j < S.clipFrames && ok; j++) {
                            State st = k.orbit.at(cl.t0 + j * cl.step);
                            ok = Pose{st.p, pointing(k.lvlh(st), cl.tilt, cl.az, 0), cl.fovDeg}.filled();
                        }
                    }
                }
                clips.push_back(cl);
            }
    std::string plan = "clip_id,epoch_k,fov_class,fov_deg,t0_s,t0_utc,step_s,tilt_deg,azimuth_deg\n";
    for (auto& c : clips) plan += fmt("%s,%d,%d,%.6f,%.0f,%s,%g,%.6f,%.6f\n", c.id.c_str(), c.k, c.fov, c.fovDeg, c.t0, k.orbit.utc(c.t0).c_str(), c.step, c.tilt, c.az);
    writeText(S.root + "/plan/S2-clips-" + o.name + ".csv", plan);

    // the sample: the first clips of each field of view at the first Sun epoch; rendering in time order, so clips that overlap share terrain
    std::map<int, int> taken;
    std::vector<Clip> used;
    for (auto& c : clips)
        if (!S.sample || (c.k == S.epochs[0] && taken[c.fov]++ < S.sampleClips)) used.push_back(c);
    std::stable_sort(used.begin(), used.end(), [](const Clip& a, const Clip& b) { return a.t0 < b.t0; });
    std::vector<Shot> shots;
    for (auto& c : used)
        for (int j = 0; j < S.clipFrames; j++) {
            Shot s;
            s.t = c.t0 + j * c.step, s.et = k.et0 + s.t, s.utc = k.orbit.utc(s.t), s.epoch = c.k, s.shift = shiftSeconds(S, c.k) / 86400;
            State st = k.orbit.at(s.t);
            s.dataset = "S2-clip", s.split = "train", s.orbit = o.name, s.clip = c.id, s.fovClass = c.fov, s.lvlh = true;
            s.tilt = c.tilt, s.azimuth = c.az, s.pose = {st.p, pointing(k.lvlh(st), c.tilt, c.az, 0), c.fovDeg};
            s.sun = ephem::sun(s.et + shiftSeconds(S, c.k));
            s.id = "S2-clip-" + o.name + "-" + c.id + "-" + stem(s.utc), s.path = "S2/clips/" + o.name + "/" + c.id + "/" + stem(s.utc);
            shots.push_back(s);
        }
    return shots;
}

std::vector<Shot> windowShots(Generator& g, size_t oi) {
    const Settings& S = g.set;
    const Track& k = track(S, oi);
    const OrbitSpec& o = *k.spec;
    double end = k.orbit.end(), testStart = end - S.testHours * 3600, valStart = testStart - S.valHours * 3600;
    struct Span { const char* split; double t0, t1; };
    std::vector<Span> spans = S.sample ? std::vector<Span>{{"test", testStart, testStart + S.sampleWindowHours * 3600}}
                                       : std::vector<Span>{{"val", valStart, testStart - 1e-6}, {"test", testStart, end}};

    // profile B: tilt and azimuth per revolution and field of view, filled over the whole revolution: at its highest altitude, where the
    // limb comes closest to the boresight (the specs say lowest, which does not keep the limb out); drawn from the
    // validation window on, so a sample has the attitudes of the full set
    std::map<std::pair<int, size_t>, glm::dvec2> att;
    for (size_t f = 0; f < o.fovs.size(); f++) {
        Rng rng(S.profileSeed + 100 * oi + f);
        for (int r = k.rev(valStart); r <= k.rev(end); r++) {
            double a = r ? k.nodes[r - 1] : k.orbit.begin(), b = size_t(r) < k.nodes.size() ? k.nodes[r] : end, high = 0;
            for (double t = a; t < b; t += 10) high = std::max(high, glm::length(k.orbit.at(t).p));
            glm::dvec2 v;
            do v = {rng(0, S.tiltMax), rng(0, 360)};
            while (!Pose{glm::dvec3(0, 0, high), pointing(glm::dmat3(1), v.x, v.y, 0), double(o.fovs[f])}.filled());
            att[{r, f}] = v;
        }
    }

    std::vector<Shot> shots;
    for (auto& sp : spans) {
        for (const std::string& combo : S.combos) {  // window truth at 1 s
            char profile = combo[0];
            int e = std::stoi(combo.substr(1));
            SunPath sun(k, sp.t0, sp.t1, shiftSeconds(S, e));
            std::string text = std::string(TRUTH) + ",sun_me_x,sun_me_y,sun_me_z,beta_deg,lit,rev";
            if (profile == 'B')
                for (int f : o.fovs) text += fmt(",tilt_%d_deg,azimuth_%d_deg", f, f);
            text += "\n";
            for (double t = std::ceil(sp.t0); t <= sp.t1; t++) {
                glm::dvec3 h(0), s = sun.at(t);
                text += truthRow(k, t, &h) + fmt(",%.9f,%.9f,%.9f,%.6f,%d,%d", s.x, s.y, s.z, glm::degrees(std::asin(glm::dot(h, s))),
                                                 int(glm::dot(glm::normalize(k.orbit.at(t).p), s) >= 0), k.rev(t));
                if (profile == 'B')
                    for (size_t f = 0; f < o.fovs.size(); f++) text += fmt(",%.6f,%.6f", att[{k.rev(t), f}].x, att[{k.rev(t), f}].y);
                text += "\n";
            }
            writeText(S.root + fmt("/S2/windows/%s/%s/window_%s_%s_%c_%d.csv", o.name.c_str(), sp.split, o.name.c_str(), sp.split, profile, e), text);
        }
        for (size_t f = 0; f < o.fovs.size(); f++)
            for (double t = sp.t0; t <= sp.t1; t += o.steps[f])
                for (const std::string& combo : S.combos) {
                    Shot s;
                    char profile = combo[0];
                    s.epoch = std::stoi(combo.substr(1)), s.rev = k.rev(t), s.t = t, s.et = k.et0 + t, s.utc = k.orbit.utc(t), s.shift = shiftSeconds(S, s.epoch) / 86400;
                    glm::dvec2 a = profile == 'B' ? att[{s.rev, f}] : glm::dvec2(0);
                    State st = k.orbit.at(t);
                    s.dataset = "S2-window", s.split = sp.split, s.windowSplit = sp.split, s.profile = std::string(1, profile), s.orbit = o.name;
                    s.fovClass = o.fovs[f], s.lvlh = true, s.tilt = a.x, s.azimuth = a.y, s.pose = {st.p, pointing(k.lvlh(st), a.x, a.y, 0), double(o.fovs[f])};
                    s.sun = ephem::sun(s.et + shiftSeconds(S, s.epoch));
                    std::string tag = fmt("%s/%c_k%d/%d/", sp.split, profile, s.epoch, s.fovClass);
                    s.path = "S2/windows/" + o.name + "/" + tag + stem(s.utc);
                    s.id = fmt("S2-window-%s-%s-%c_k%d-%d-", o.name.c_str(), sp.split, profile, s.epoch, s.fovClass) + stem(s.utc);
                    shots.push_back(s);
                }
    }
    // all fields of view and combos of a moment together: the satellite is in one place at a time
    std::stable_sort(shots.begin(), shots.end(), [](const Shot& a, const Shot& b) { return a.t < b.t; });
    return shots;
}

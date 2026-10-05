#pragma once
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>
#include "config.h"

struct OrbitSpec { std::string name, file; std::vector<int> fovs; std::vector<double> steps; };

// generator settings (linna2/config.ini); a key that is not given keeps the value of the LINNA-2 specs
struct Settings {
    std::string output, sampleOutput, root;  // root: output of this run (sampleOutput with --sample)
    bool sample = false;
    int bench = 0;  // S1: only the first bench frames of each full split, in rendering order (throughput of the full run)
    int renderRes = 1024, imageRes = 512, writers = 3;
    std::vector<int> fovClasses{24, 30, 60, 90};
    double fovJitter = 0.1, tiltMax = 30, nightDeg = -7, shadowReachKm = 150, waitLimitS = 300;
    // S1
    std::vector<int> s1Count{150000, 15000, 15000}, s1Sample{300, 50, 50};
    std::vector<unsigned long long> s1Seed{11, 12, 13};
    double polarShare = 0.2, polarLat = 60, gsdMinM = 30, capMarginDeg = 1.54;
    std::vector<double> altitudeKm{15, 200}, sunShare{0.15, 0.10, 0.45, 0.30}, sunEdges{-10, -3, 3, 15, 45};
    int limbRetries = 20;
    // S2
    std::vector<OrbitSpec> orbits;
    int clipsPerEpoch = 500, clipFrames = 48, sampleClips = 1;
    std::vector<int> epochs{0, 1, 2, 3, 4, 5};
    std::vector<std::string> combos{"A0", "A3", "B0"};
    double guardHours = 1, valHours = 24, testHours = 72, siderealDays = 27.321661, sampleWindowHours = 2;
    unsigned long long clipSeed = 21, profileSeed = 31, reproSeed = 41;
    // labels
    int rimSamples = 64;
    double emissionMaxDeg = 60, dEffMinKm = 1.5, sizeMinPx = 10, sizeMaxFrac = 0.4, litPos = 0.5, litBg = 0.1, inFrameMin = 0.5;
    double litTau = 0.01, litPercentile = 99.5, darkLevel = 0.01, darkShare = 0.05;
};

template <class T> void parseValue(std::istream& in, T& v) {
    if constexpr (std::is_same_v<T, std::string>) {
        std::getline(in >> std::ws, v);
        v.erase(v.find_last_not_of(" \t\r") + 1);
    } else if constexpr (requires { v.push_back(v[0]); }) {
        v.clear();
        for (typename T::value_type x; in >> x;) v.push_back(x);
    } else in >> v;
}

// kv: the keys of the configuration file that are not the simulator's (loadConfig's rest)
inline Settings loadSettings(std::map<std::string, std::string> kv) {
    Settings s;
    auto take = [&](const std::string& key, auto& v) {
        if (auto it = kv.find(key); it != kv.end()) {
            std::istringstream in(it->second);
            parseValue(in, v);
            kv.erase(it);
        }
    };
    take("output_dir", s.output), take("sample_dir", s.sampleOutput), take("render_res", s.renderRes), take("image_res", s.imageRes);
    take("writer_threads", s.writers), take("fov_classes", s.fovClasses), take("fov_jitter", s.fovJitter), take("tilt_max_deg", s.tiltMax);
    take("night_shortcut_deg", s.nightDeg), take("shadow_reach_km", s.shadowReachKm), take("terrain_wait_limit_s", s.waitLimitS);
    take("s1_frames", s.s1Count), take("s1_sample_frames", s.s1Sample), take("s1_seeds", s.s1Seed), take("s1_polar_share", s.polarShare);
    take("s1_polar_lat_deg", s.polarLat), take("s1_gsd_min_m", s.gsdMinM), take("s1_sun_cap_margin_deg", s.capMarginDeg);
    take("s1_altitude_km", s.altitudeKm), take("s1_sun_shares", s.sunShare), take("s1_sun_edges_deg", s.sunEdges), take("s1_limb_retries", s.limbRetries);
    take("s2_clips_per_epoch", s.clipsPerEpoch), take("s2_clip_frames", s.clipFrames), take("s2_sample_clips", s.sampleClips);
    take("s2_epochs", s.epochs), take("s2_window_combos", s.combos), take("s2_guard_hours", s.guardHours), take("s2_val_hours", s.valHours);
    take("s2_test_hours", s.testHours), take("sidereal_month_days", s.siderealDays), take("s2_sample_window_hours", s.sampleWindowHours);
    take("s2_clip_seed", s.clipSeed), take("s2_profile_b_seed", s.profileSeed), take("repro_seed", s.reproSeed);
    take("rim_samples", s.rimSamples), take("emission_max_deg", s.emissionMaxDeg), take("d_eff_min_km", s.dEffMinKm), take("size_min_px", s.sizeMinPx);
    take("size_max_frac", s.sizeMaxFrac), take("lit_pos", s.litPos), take("lit_bg", s.litBg), take("in_frame_min", s.inFrameMin);
    take("lit_tau", s.litTau), take("lit_percentile", s.litPercentile), take("dark_level", s.darkLevel), take("dark_share", s.darkShare);
    std::vector<std::string> names;
    take("orbits", names);
    for (auto& n : names) {
        OrbitSpec o{n};
        take("orbit_" + n, o.file), take("orbit_" + n + "_fovs", o.fovs), take("orbit_" + n + "_steps_s", o.steps);
        if (o.file.empty() || o.fovs.empty() || o.fovs.size() != o.steps.size()) fatal("linna2: orbit " + n + " needs orbit_" + n + ", _fovs and _steps_s");
        s.orbits.push_back(o);
    }
    for (auto& [k, v] : kv) fprintf(stderr, "config: unknown key '%s'\n", k.c_str());
    return s;
}

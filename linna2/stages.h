#pragma once
#include "generator.h"

// renders the shots of a set that its index does not have yet, then logs the run in index/runs.csv
void runSet(Generator& g, const std::string& set, const std::vector<Shot>& shots);
std::string readText(const std::string& path);
void writeText(const std::string& path, const std::string& text);
std::string stem(const std::string& utc);  // ISO UTC -> 20290809T212200
std::vector<std::map<std::string, std::string>> readCsv(const std::string& path);

void catalogStage(Generator& g);  // e_rim of every catalogue crater -> catalog/robbins_linna2.csv (04 §1)
void angleCheck(Generator& g);    // 04 §6: 20 elliptical craters with their projected ellipses -> qc/angle_check
std::vector<Shot> s1Shots(Generator& g, int split);   // 02: the split drawn from its seed (plan/S1-<split>.csv), in rendering order
void truthStage(const Settings& s);                   // 03 §2-3: truth, beta and dark arcs per orbit and Sun epoch
std::vector<Shot> clipShots(Generator& g, size_t orbit);    // 03 §4 (plan/S2-clips-<orbit>.csv)
std::vector<Shot> windowShots(Generator& g, size_t orbit);  // 03 §5-6, with the window truth files
bool leakageCheck(const Settings& s);                       // 05 §5
void writeManifest(const Settings& s, const Config& c, const std::string& version, const std::string& hash);
void repro(Generator& g, int n);  // 06 #13: n done frames rendered again into qc/repro and compared

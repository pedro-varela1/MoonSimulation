SHELL := cmd.exe
CC := gcc
CXX := g++
CS := third_party/cspice
CSDEF := -std=gnu99 -fpermissive -w -DMSDOS -DOMIT_BLANK_CC -D_COMPLEX_DEFINED -I$(CS)/include
CXXFLAGS := -std=c++20 -O2 -Wall -MMD -isystem third_party -isystem $(CS)/include
LDLIBS := -lglfw3 -lglew32 -lopengl32 -ltiff -lpng -lz -lcomdlg32 -lole32 -luuid
SUBSYSTEM ?= -mwindows
APP := $(patsubst src/%.cpp,build/%.o,$(wildcard src/*.cpp))
GUI := $(patsubst third_party/imgui/%.cpp,build/imgui/%.o,$(wildcard third_party/imgui/*.cpp))
CSO := $(patsubst $(CS)/src/cspice/%.c,build/cspice/%.o,$(wildcard $(CS)/src/cspice/*.c))

LINNA := $(patsubst linna2/%.cpp,build/linna2/%.o,$(wildcard linna2/*.cpp))

# SUBSYSTEM= (empty) builds a console executable for debugging
MoonSimulation.exe: $(APP) $(GUI) build/app.res build/libcspice.a
	$(CXX) $(SUBSYSTEM) -o $@ $^ $(LDLIBS)

# LINNA-2 dataset generator (console): the simulator's code without its window and UI
linna2/linna2.exe: $(LINNA) $(filter-out build/main.o build/ui.o,$(APP)) build/libcspice.a
	$(CXX) -o $@ $^ $(LDLIBS) -lbcrypt

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/linna2/%.o: linna2/%.cpp | build/linna2
	$(CXX) $(CXXFLAGS) -Isrc -c $< -o $@

build/app.res: src/app.rc installer/selenita.ico | build
	windres $< -O coff -o $@

build/imgui/%.o: third_party/imgui/%.cpp | build/imgui
	$(CXX) -std=c++20 -O2 -w -c $< -o $@

build/cspice/%.o: $(CS)/src/cspice/%.c | build/cspice
	$(CC) -O2 -DNON_ANSI_STDIO $(CSDEF) -c $< -o $@

build/cspice/zzsecprt.o: $(CS)/src/cspice/zzsecprt.c | build/cspice
	$(CC) -O0 $(CSDEF) -c $< -o $@

build/libcspice.a: $(CSO)
	$(file >build/cspice.rsp,$^)
	ar rcs $@ @build/cspice.rsp

build build/cspice build/imgui build/linna2:
	mkdir $(subst /,\,$@)

-include $(APP:.o=.d) $(LINNA:.o=.d)

SHELL := cmd.exe
CC := gcc
CXX := g++
CS := third_party/cspice
CSDEF := -std=gnu99 -fpermissive -w -DMSDOS -DOMIT_BLANK_CC -D_COMPLEX_DEFINED -I$(CS)/include
CXXFLAGS := -std=c++20 -O2 -Wall -MMD -isystem third_party -isystem $(CS)/include
LDLIBS := -lglfw3 -lglew32 -lopengl32 -ltiff -lz
APP := $(patsubst src/%.cpp,build/%.o,$(wildcard src/*.cpp))
CSO := $(patsubst $(CS)/src/cspice/%.c,build/cspice/%.o,$(wildcard $(CS)/src/cspice/*.c))

MoonSimulation.exe: $(APP) build/libcspice.a
	$(CXX) -o $@ $^ $(LDLIBS)

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/cspice/%.o: $(CS)/src/cspice/%.c | build/cspice
	$(CC) -O2 -DNON_ANSI_STDIO $(CSDEF) -c $< -o $@

build/cspice/zzsecprt.o: $(CS)/src/cspice/zzsecprt.c | build/cspice
	$(CC) -O0 $(CSDEF) -c $< -o $@

build/libcspice.a: $(CSO)
	$(file >build/cspice.rsp,$^)
	ar rcs $@ @build/cspice.rsp

build build/cspice:
	mkdir $(subst /,\,$@)

-include $(APP:.o=.d)

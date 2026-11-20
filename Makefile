# ==============================================================================
# Engram-Core Makefile
# Build targets for research gates, systems benchmarks, and tests.
# ==============================================================================

CXX = cl
CXXFLAGS = /nologo /std:c++23 /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS /I src/core /I src/benchmark /I vendor/sqlite3/sqlite-amalgamation-3470200
LIBS = Psapi.lib Winmm.lib user32.lib
STACK = /link /STACK:16777216

all: systems_benchmark b1_gauntlet heartbeat r0_baseline

systems_benchmark: build/systems_benchmark.exe

build/sqlite3.obj: vendor/sqlite3/sqlite-amalgamation-3470200/sqlite3.c
	@if not exist build mkdir build
	cl /nologo /O2 /c vendor/sqlite3/sqlite-amalgamation-3470200/sqlite3.c /Fobuild/sqlite3.obj

build/systems_benchmark.exe: src/benchmark/systems_benchmark.cpp build/sqlite3.obj
	@if not exist build mkdir build
	$(CXX) $(CXXFLAGS) src/benchmark/systems_benchmark.cpp build/sqlite3.obj /Fobuild/systems_benchmark.obj /Febuild/systems_benchmark.exe $(STACK) $(LIBS)

b1_gauntlet: src/benchmark/b1_retention_gauntlet.cpp
	@if not exist build mkdir build
	$(CXX) $(CXXFLAGS) src/benchmark/b1_retention_gauntlet.cpp /Fobuild/b1_retention_gauntlet.obj /Febuild/b1_retention_gauntlet.exe $(STACK)

heartbeat: src/benchmark/heartbeat_gate.cpp
	@if not exist build mkdir build
	$(CXX) $(CXXFLAGS) src/benchmark/heartbeat_gate.cpp /Fobuild/heartbeat_gate.obj /Febuild/heartbeat_gate.exe $(STACK) $(LIBS)

r0_baseline: src/benchmark/r0_baseline_gate.cpp
	@if not exist build mkdir build
	$(CXX) $(CXXFLAGS) src/benchmark/r0_baseline_gate.cpp src/core/fpsan_wasm_sandbox.cpp build/m3_*.obj /Fobuild/r0_baseline_gate.obj /Febuild/r0_baseline_gate.exe $(STACK) user32.lib

clean:
	@if exist build del /Q /F build\*

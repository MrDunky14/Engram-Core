CC = g++
CFLAGS = -std=c++17 -O3 -pthread

all: atomic agency hierarchy gauntlet

atomic: src/fpsan_v6_atomic.cpp
	$(CC) $(CFLAGS) src/fpsan_v6_atomic.cpp -o build/v6_atomic

agency: src/fpsan_v6_agency.cpp
	$(CC) $(CFLAGS) src/fpsan_v6_agency.cpp -o build/v6_agency

hierarchy: src/fpsan_v6_hierarchy.cpp
	$(CC) $(CFLAGS) src/fpsan_v6_hierarchy.cpp -o build/v6_hierarchy

gauntlet: src/fpsan_tony_stark.cpp
	$(CC) $(CFLAGS) src/fpsan_tony_stark.cpp -o build/v6_gauntlet

clean:
	rm -f build/*

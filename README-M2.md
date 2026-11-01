# PolyPitch Milestone 2

Milestone 2 deliberately leaves source/fundamental inference OFF.

It adds a scrolling temporal view of the conservative C++ observation field.
Time runs downward; pitch runs left-to-right. The lower panel remains an
instantaneous diagnostic.

Build exactly as Milestone 1:

    emcmake cmake -S . -B build -G Ninja
    cmake --build build

Copy build/polypitch.js and build/polypitch.wasm into web/, then serve web/.

Tests:
1. Sustained hum: look for a stable fundamental ridge plus parallel harmonic ridges.
2. Slowly glide the hum: harmonic ridges should move together.
3. Bass G / hammer-on: inspect which ridges start, stop and move together.
4. Later: cello C2+G2 double stop.

This is intentionally the last diagnostic step before temporal ridge/source grouping.

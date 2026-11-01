# PolyPitch C++ / WASM - Milestone 1

Clean start for the analysis engine.

This milestone deliberately stops at the observation layer: microphone PCM -> Hann FFT -> dense 10-cent local pitch evidence. It preserves the useful lesson from JS 1.7: no pitch is invented without local spectral evidence.

It does **not** yet decide fundamental vs harmonic. That is the next layer.

## Build

Install/activate Emscripten, then:

    emcmake cmake -S . -B build
    cmake --build build

The next milestone is temporal ridge/source inference: track coherent ridges, group harmonically co-moving ridges, let established sources explain their harmonics, and leave unexplained coherent ridges available as independent notes.

Regression targets:
- sustained hum: one dominant source, harmonics subordinate
- bass open G + 12/15 hammer-ons: follow played pitch
- cello C2+G2 double stop: preserve both fundamentals

## 2.0 DSP regression test
The pitch-evidence layer now has a native synthetic-signal regression harness.
On a machine with a C++ compiler/CMake:

    cmake -S . -B build-native
    cmake --build build-native
    ctest --test-dir build-native --output-on-failure

It covers a pure sine, harmonic-rich tones, weak-fundamental tones, an up/down glissando and a two-note polyphonic signal.


## 2.1 expanded DSP regression suite

2.1 deliberately tests the analyser before microphone testing. In addition to the 2.0 cases, the native suite now includes:

- a long sustained, vibrato-like hold followed by a descending phrase;
- intra-window up/down chirps, so pitch is moving during the ~171 ms FFT window rather than being stationary for each FFT;
- a deliberately awkward formant-like spectrum with weak fundamental and stronger upper partials;
- amplitude ramping during the post-hold descent;
- an octave/subharmonic trap to ensure a real 196 Hz source is not incorrectly promoted to 98 Hz.

The new chirp test exposed a real 2.0 failure: 195 -> 220 Hz was misidentified by about 7.1 semitones. The cause was harmonic association windows becoming progressively narrower for higher partials. That assumption is wrong for a moving source: a proportional pitch sweep smears every harmonic by roughly the same interval in cents. 2.1 therefore keeps the harmonic association width approximately constant in pitch space. The expanded suite passes after that change.

## 2.4 diagnostic build
This build intentionally keeps the 2.3 pitch-evidence algorithm unchanged and adds pipeline instrumentation. The on-screen diagnostic box shows frame counter, input RMS, raw spectral centroid, background/residual means, noise estimate, strongest evidence, raw peak count, strongest raw FFT components and strongest extracted peaks. Use **Export diagnostics** immediately after reproducing a freeze to save the retained diagnostic frames as CSV.

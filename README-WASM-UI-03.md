# PolyPitch WASM UI 0.3

Built from the clean 0.1 baseline.

C++:
- keeps the original conservative local evidence bank
- additionally extracts true local FFT maxima
- quadratic sub-bin interpolation
- exposes peak MIDI + strength to WASM

Browser:
- matches peaks into persistent ridge identities
- predicts short-term motion when matching
- retains tracks through brief dropouts
- draws continuous additive line segments, not per-frame dots
- immature/weak tracks remain faint
- established tracks become narrow luminous ridges

There is still NO fundamental/harmonic classification in this build.
The purpose is to establish reliable continuous trajectories first.

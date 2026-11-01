# PolyPitch WASM UI 0.2
First C++ temporal layer.

Raw local evidence is unchanged from 0.1. A separate per-lane temporal
coherence field now tracks persistence and continuity across analysis frames.
No harmonic suppression or fundamental inference is performed.

Pitch Focus view:
- raw observation: faint
- persistent/coherent ridge evidence: bright

Evidence view:
- raw observation emphasized for comparison

Regression test: hum, short scale, sustained note, then bass hammer-on.

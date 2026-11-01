# PolyPitch WASM UI 0.5

Built from the 0.4/0.3 ridge baseline.

Changes:
- C++ spectral peak extraction remains unchanged.
- Keeps 0.4 ridge continuity/dropout bridging.
- Adds ~14-frame ridge motion history.
- Harmonic grouping now tolerates small instantaneous mismatch and uses movement agreement across history.
- A source must be an actually observed ridge; no missing fundamentals are invented.
- Strong coherent source activity raises dynamic competition.
- Weak unrelated ridges collapse aggressively during a strong note.
- Explained harmonics remain visible but subdued.
- Multiple independently strong persistent ridges are protected for double stops.
- Readout chooses highest-confidence source.

Primary test: room noise -> E3 -> G3 -> C3 hum.
Expected visual behaviour: faint cloud -> strong note -> cloud collapses around coherent source/family.

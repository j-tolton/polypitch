# PolyPitch WASM UI 0.6

0.6 moves room-tone rejection to the DSP front end.

- Per-frequency-bin adaptive background model in C++.
- Fast acquisition for roughly the first 1.2 seconds.
- Slow adaptation thereafter.
- Sudden rises >6 dB learn much more slowly, preventing a played sustained note from immediately becoming "background".
- 3.5 dB residual margin removes stationary fan/computer/electrical structure.
- Peak extraction and pitch evidence now operate on residual spectrum above learned background.
- 0.5 history-aware grouping/evidence competition remains downstream.

Regression:
1. Start with webcam room tone only.
2. Initial evidence may appear.
3. By ~1-2 seconds the display should become dramatically quieter / near blank.
4. Hum E3-G3-C3; coherent tracks should appear immediately.
5. Stop; residual evidence should decay back toward blank.

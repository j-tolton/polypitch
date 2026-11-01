# PolyPitch WASM UI 0.6.2

Built from 0.6; 0.6.1 discarded.

- First ~1 second is an explicit calibration phase.
- Background spectrum learns unconditionally and quickly during calibration.
- No pitch evidence, peaks, tracks or visible history are produced during calibration.
- After calibration, slow asymmetric adaptation resumes.
- Sudden >6 dB rises learn extremely slowly so sustained notes remain foreground.
- Uses 0.6 residual model rather than 0.6.1's aggressive HF changes.
- UI displays CALIBRATING and starts with a clean history.

Test: start in ordinary webcam room tone, wait for CALIBRATING to clear, confirm near-blank display, then hum E3-G3-C3.

# PolyPitch WASM UI 0.6.1

Calibration pass on 0.6; architecture unchanged.

- Initial room-tone learning shortened to roughly 0.7 seconds.
- Residual evidence now requires both rise above learned background and adequate absolute spectral energy.
- Absolute requirement increases progressively in the high-frequency region.
- Peak admission becomes modestly stricter above 1.2 kHz and again above 4 kHz.
- New high-frequency browser tracks need slightly stronger evidence.
- Existing track maintenance and low/mid-frequency hum behaviour are otherwise unchanged.

Regression:
room tone -> quickly near blank, with no intermittent upper-frequency forest;
then E3-G3-C3 hum -> preserve 0.6's clean coherent trajectories.

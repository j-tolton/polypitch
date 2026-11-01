# PolyPitch WASM UI 0.7

Built directly from the calibrated 0.6.2-final baseline. DSP/calibration unchanged.

Rendering confidence is now explicit:
- source = continuous luminous ridge
- harmonic = faint compact particle cloud
- raw/unexplained = still fainter, broader cloud
- non-source evidence is never connected frame-to-frame

This is deliberately a visual/semantic change, not another detector retune. The close live source view should remain familiar while zoomed-out history should lose the distracting forest of harmonic/noise lines.

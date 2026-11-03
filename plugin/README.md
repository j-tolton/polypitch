# PolyPitch plugin

The PolyPitch trace as a VST3, to put on a vocal or instrument track. It shares its analysis with the web app (`dsp/polypitch.cpp` and `dsp/tracker.cpp`). The sound passes through untouched and the plugin adds no latency.

## Build

Needs Visual Studio 2022, CMake, Ninja and JUCE at `C:\Development\JUCE`.

```
plugin\build.bat
```

The VST3 lands in `plugin\build\PolyPitch_artefacts\Release\VST3`. To use it in Reaper, add that folder under Preferences > Plug-ins > VST and re-scan. Close Reaper before rebuilding, because it holds the file open.

To copy to the system VST3 folder instead, configure with `-DPOLYPITCH_COPY_PLUGIN=ON` and build from an elevated prompt.

## Using it

- Drag sideways to look back, or up and down to move through pitch.
- Scroll to move through pitch, Shift-scroll to move through time, Ctrl-scroll to zoom pitch and Alt-scroll to zoom time.
- Point at the trace when it isn't moving to read the note under the cursor.
- The trace is kept for five minutes while the plugin is loaded. The scale and view settings are saved with the project, but the trace is not.

## Tests

```
plugin\build\PolyPitchRenderTest_artefacts\Release\PolyPitchRenderTest.exe some-mono-48k.wav
plugin\build\PolyPitchHostTest_artefacts\Release\PolyPitchHostTest.exe plugin\build\PolyPitch_artefacts\Release\VST3\PolyPitch.vst3
```

The first runs audio through the plugin's classes at several sample rates and block sizes, and compares the analysis with the engine run directly. The second loads the built VST3 as a host would, checks the sound is unchanged and opens the window.

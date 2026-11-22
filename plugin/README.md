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

- In Reaper it reads the track it sits on and analyses it in the background when the window first opens, so the whole trace is there to navigate. Click to move the playhead, and use Play or Reaper's own transport. If the audio changes, it offers to analyse again.
- Tracks are analysed with two window lengths, a long one below E3 where low notes need it and a short one above, which follows quick notes and separates voices better. Listening live uses the long one alone.
- Once analysed, the trace is cleaned up, starting with anything 30 dB or more below the track's playing level, using the whole track: what doesn't last, connect to a note or keep returning to one pitch alongside another voice is removed, along with faint harmonics that shadow a louder note. Quieter voices are lifted to their own level. Fainter lines that have their own spectral peak, off the harmonic series of anything louder, are drawn dimmer: they are probably a second voice, but could be something else in the recording. The Clean button switches back to everything as first heard.
- It suggests a key and, if the track isn't at A=440, its tuning. The suggestion disappears once you've chosen a scale. The View menu sets the reference pitch: the note lines move with it, and in tune is judged against it.
- With a project tempo, the time axis shows Reaper's bars, with beat lines when they're far enough apart.
- A finished analysis is kept in `%LOCALAPPDATA%\PolyPitch\Analyses`, so the same audio opens at once next time. A fingerprint of the track's audio decides whether it still applies, and the folder is kept under 300 MB.
- Every PolyPitch on a track shows the others' notes in their own colours, so harmony parts can be read together. This can be turned off in the View menu.
- On a track with no audio, or in another DAW, it listens to the track as it plays instead.
- Drag sideways to move through time, or up and down to move through pitch.
- Scroll to move through pitch, Shift-scroll to move through time, Ctrl-scroll to zoom pitch and Alt-scroll to zoom time.
- Point at the trace when it isn't moving to read the note under the cursor.
- The scale and view settings are saved with the project, but the trace is not. It is analysed again when you reopen the window. When listening, it keeps the last five minutes.

## Tests

```
plugin\build\PolyPitchRenderTest_artefacts\Release\PolyPitchRenderTest.exe some-mono-48k.wav
plugin\build\PolyPitchHostTest_artefacts\Release\PolyPitchHostTest.exe plugin\build\PolyPitch_artefacts\Release\VST3\PolyPitch.vst3
```

The first runs audio through the plugin's classes at several sample rates and block sizes, and compares the analysis with the engine run directly. The second loads the built VST3 as a host would, checks the sound is unchanged and opens the window.

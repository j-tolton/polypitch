# PolyPitch

A pitch trace and tuner for practising an instrument or singing. Play or sing, and the notes you produce are drawn as lines scrolling across a grid of note names. They turn green when they're in tune with the scale or arpeggio you're practising. More than one note at a time is followed, so double stops, chords and harmony parts show up as separate lines.

It comes in two forms that share one analysis engine:

- **The web app** (`web/`), which works on a phone or computer and offline once opened. It's live at https://polypitch.pages.dev/.
- **Android apps** (`app/`), which wrap the web app as an installable APK. See [app/README.md](app/README.md).
- **A VST3 plugin** (`plugin/`), which draws the same trace for a track in Reaper or another DAW. See [plugin/README.md](plugin/README.md).

## The web app

- **Trace.** Listen starts the microphone, and Stop then Listen carries on the same trace. Play plays back what was heard, and Clear starts again. You can also open a recording, or drop one on the page. Drag, scroll or pinch to move back through time and zoom in on pitch.
- **Practise.** Choose a root note and a scale or arpeggio. Notes that belong to it are marked on the grid, and notes played in tune turn green.
- **Instrument.** This sets the range shown and ignores sound below the instrument's lowest note. Clarinet, saxophones, trumpet and baritone can name notes as written on their music, or at concert pitch.
- **Tuner.** The tuning-fork button opens a tuner that is always listening. It has three tabs:
  - **Chromatic:** the nearest note on a sliding tape, with a strobe underneath for the last few cents.
  - **Strings:** each string of the chosen tuning, ticked once it's in tune.
  - **Strum:** reads every string from a single strum.

  The reference pitch (A = 440) can be moved up or down.

The full controls are listed under Settings in the app.

### Files

| File | What it is |
|---|---|
| `index.html` | The whole app: page, styles and code |
| `polypitch.js`, `polypitch.wasm` | The analysis engine, built from `dsp/` |
| `audio-worklet.js` | Passes microphone audio to the page |
| `strum.js` | Reads a strummed chord; also used by the strum tests |
| `sw.js`, `manifest.webmanifest`, `icon-*.png` | Let it install as an app and work offline |
| `.well-known/assetlinks.json` | Tells Chrome the Android app is allowed to show the site full screen |

### Deploying

Cloudflare Pages publishes the `web` folder from the `master` branch on GitHub. A push is live within about half a minute. There's no build step for the page itself. Only the engine is built, and `polypitch.js` and `polypitch.wasm` are committed already built.

For local testing, serve `web/` from any static server. The microphone needs `localhost` or HTTPS.

## The analysis engine

`dsp/` is C++ shared by the web app and the plugin:

- `polypitch.cpp`: turns audio into pitch evidence. It takes an 8192-point FFT every 1024 samples, then measures pitch in 10-cent lanes, grouping each note's harmonics so they aren't mistaken for extra notes.
- `tracker.cpp`: links that evidence from frame to frame into ridges, one per note, and decides which are in tune.
- `refine.cpp`, `dual_engine.h`: whole-track clean-up and the two-window analysis used by the plugin.
- `wasm_api.cpp`: the functions the web app calls.

### Building the WebAssembly

Needs [Emscripten](https://emscripten.org/) at `C:\Development\emsdk`, plus CMake and Ninja.

```
build.bat
```

This builds into `build/` and copies `polypitch.js` and `polypitch.wasm` into `web/`.

### Tests

The native test feeds synthetic signals through the engine: sines, harmonic-rich and weak-fundamental tones, glides and two-note chords. It checks that each note is found at the right pitch and not an octave out.

```
cmake -S . -B build-native
cmake --build build-native
ctest --test-dir build-native --output-on-failure
```

The plugin has its own tests, described in its README.

## Layout

```
web/      the web app
dsp/      the shared analysis engine
tests/    native engine tests
app/      the Android apps
plugin/   the VST3 plugin (JUCE)
```

Version history is in the commit log. `status.odt` holds early design notes and is out of date.

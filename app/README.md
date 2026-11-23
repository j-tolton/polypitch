# PolyPitch for Android

There are two Android builds of the web app. Both are installable APKs with the PolyPitch icon and a dark launch screen.

| | `twa/`: Trusted Web Activity | `capacitor/`: Capacitor |
|---|---|---|
| What it runs | The live site, https://polypitch.pages.dev/, full screen in Chrome | The files in `web/`, bundled inside the app, in Android's WebView |
| Updates | Every push to `master` reaches it straight away | Needs a rebuild and reinstall |
| Offline | After the first launch, through the site's service worker | Always |
| Microphone prompt | Two prompts: one for the site, one for Chrome | One prompt, for PolyPitch |
| Package name | `app.polypitch.web` | `app.polypitch` |

The two builds have different package names, so both can be installed side by side.

## What you need

- **JDK 21.** Microsoft's build is unpacked at `%LOCALAPPDATA%\Android\jdk21`.
- **The Android SDK** at `%LOCALAPPDATA%\Android\Sdk`, with platform 36 and build-tools 36.1.0.
- **Node**, for Capacitor.

Before building, set these in PowerShell:

```powershell
$env:JAVA_HOME="$env:LOCALAPPDATA\Android\jdk21"; $env:ANDROID_HOME="$env:LOCALAPPDATA\Android\Sdk"
```

## The signing key

Release builds are signed with the key in `%USERPROFILE%\.polypitch\`:

- `polypitch-release.jks` is the key itself.
- `keystore.properties` holds its path and password.

Both files are kept out of the repo. **Back them up.** Android only installs an update over an existing app if it's signed with the same key. The key's fingerprint is also listed in `web/.well-known/assetlinks.json`, which is how Chrome knows the wrapper app and the site belong together.

If the key is missing, release builds come out unsigned and can't be installed.

## Building

**Trusted Web Activity:**

```powershell
cd app\twa; .\gradlew.bat assembleRelease
```

The APK is `app\twa\app\build\outputs\apk\release\app-release.apk`.

**Capacitor.** This copies `web/` into the app, then builds:

```powershell
cd app\capacitor; npm install; npx cap sync android; cd android; .\gradlew.bat assembleRelease
```

The APK is `app\capacitor\android\app\build\outputs\apk\release\app-release.apk`.

For the Play Store, build with `bundleRelease` instead of `assembleRelease`. That makes an `.aab` file instead of an APK.

## Installing on a phone

Copy the APK to the phone and open it. Android asks once to allow installing apps from whichever app opened the file, such as Files or Chrome. Alternatively, with USB debugging on, run `adb install app-release.apk`.

## Icons

`make-icons.js` draws the same picture as `web/icon-512.png` at every size Android needs, using headless Edge:

```powershell
cd app; node make-icons.js capacitor\android\app\src\main\res twa\app\src\main\res
```

## Known gaps in the Capacitor build

- **Export buttons.** "Export last 20 s" and "Export diagnostics" do nothing, because a WebView doesn't handle downloads. Fixing this needs a native plugin to save or share the file.
- **Back button.** It closes the app instead of closing an open panel.

# Android build

GT4 Revisited for Android phones and tablets: one APK for arm64-v8a and x86_64, Android 7.0
(API 24) and later, with a Vulkan driver.

Requirements: the Android SDK with NDK 27.0.12077973 (the version `app/build.gradle` pins) and
CMake 3.22.1 or later (install both from the SDK Manager), and **JDK 17 or 21** (the Gradle
wrapper, 8.12, does not run on JDK 24 or later).

```bash
android/fetch-sdl.sh
```
```bash
cd android && ./gradlew assembleDebug
```

The APK is written to `app/build/outputs/apk/debug/`. Install it with
`adb install -r app/build/outputs/apk/debug/app-debug.apk`. From a git worktree (no
`local.properties`), set `ANDROID_HOME` (macOS: `~/Library/Android/sdk`) first.

`fetch-sdl.sh` checks out SDL3, at the release the shared engine pins, into
`android/third_party/SDL`. That one checkout supplies both SDL's Java glue (`org.libsdl.app`)
and the native library, so the two always match. The top-level `CMakeLists.txt` builds the game
as `libmain.so`, which SDL's activity loads.

What the app is made of:

- The game's native code: the same sources as the desktop game.
- The shared engine's activity code (`engine/gt3r/android/app/src/main/java`, compiled from the
  submodule as it is): SDL's activity, read access to the chosen disc image kept across launches,
  the save folder through the Storage Access Framework, the user data folder in the Files app
  (a documents provider), the display's cutout and corners. `GT4Activity` is the app's own
  activity on top of it. The engine's Java reads its resources through `org.gt3revisited.gt3r.R`,
  so that is the Gradle `namespace`; the app's own identity is its application id,
  `org.gt4revisited.gt4r`, its name "GT4 Revisited" and its icon (`packaging/make_icon.py`).
- The version comes from the top-level `CMakeLists.txt`: versionName `x.y.z`, versionCode
  `x*10000 + y*100 + z`.

## Using it

1. Copy your Gran Turismo 4 `.iso` or `.chd` image to the device (or an SD card or USB drive).
2. Launch the app and choose **Open disc image**: Android's file picker opens, and the app reads
   the image through the Storage Access Framework, so no storage permission is needed. The image
   is remembered.
3. Bluetooth and USB controllers work at once. Without one, on-screen touch controls appear: a
   D-pad and face buttons over the game's menus, the driving controls in a race. They hide when
   a controller is used. The app's own pages (the home screen, Settings) are tapped.

The Android back button is Triangle (back). Phones and tablets start with dynamic resolution on
and 2x anti-aliasing (devices without 2x get 4x).

The user data folder (settings, saves, mods, screenshots, `gt4r.log`) is the app's own storage;
the Files app shows it as "GT4 Revisited".

## Testing on a device or the emulator

A debug APK can be driven like the desktop game's scripted runs: `files/args.txt` in the app's
storage holds one command-line argument a line, and `--env NAME=value` sets a variable the run
reads. For example, with the disc image copied to `/data/local/tmp/gt4.iso`
(`adb push`, then `adb shell chmod 644 /data/local/tmp/gt4.iso`):

```bash
printf '/data/local/tmp/gt4.iso\n--screen\ngame\n' > args.txt
adb push args.txt /data/local/tmp/
adb shell run-as org.gt4revisited.gt4r sh -c 'mkdir -p files && cp /data/local/tmp/args.txt files/'
adb shell am start -n org.gt4revisited.gt4r/.GT4Activity
```

A fresh install has no `files/` folder: make it first, as above. `adb logcat -s SDL GT4R` shows
the log (the same lines as `gt4r.log`); `adb exec-out screencap -p > shot.png` takes a picture.
Remove `files/args.txt` afterwards.

## Release signing

Release APKs are signed with the project's own key, so every release can update an installed
copy. Create it once with `android/make-release-key.sh` (the certificate names the project, not
a person) and keep the `.jks` and its password in a password manager. Then set `GT4R_KEYSTORE`
(its path) and `GT4R_KEYSTORE_PASSWORD` (and `GT4R_KEY_ALIAS` when it is not `gt4r`) before
`./gradlew assembleRelease`; without them the release APK is unsigned. Debug builds use the
local debug key, so a debug and a release build cannot update each other: uninstall one first
(which removes the app's storage: move the saves out first).

## Meta Quest

What a GT4 Revisited flavor for Meta Quest headsets needs (docs/PLAYING.md "What runs where"):

- The phone APK itself installs on a Quest in developer mode and runs as a flat 2D app window
  (arm64, Vulkan), played with a Bluetooth controller. Not tried on a headset.
- An immersive VR app as the shared engine's own Quest APK is: the build plumbing (SDL's
  development branch with SDL_GPU's OpenXR support, `fetch-sdl.sh --xr`; `GT3R_XR` with the
  OpenXR headers on the engine library; the Khronos OpenXR loader package; the headset's
  manifest with the VR launcher categories) plus app code this app does not have yet: an
  `xr::Session` (the engine's `src/xr`) made from the GPU device, every frame drawn into its
  canvas and handed to it, the controllers' input and pointer, recentering. With those the menus
  and the races would show on the engine's virtual screen. A race in stereo needs a race view
  drawn per eye from the head pose, which the engine's own game has in its race screen and this
  app's race screen does not.

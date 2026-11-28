# VR headsets (Meta Quest, PC VR)

GT4 Revisited runs in a VR headset through the shared engine's OpenXR layer (`engine/gt3r/src/xr`, its
`docs/dev/VR.md` has the session, the swapchains, the colours, the controllers and the runtimes in detail). The game's
menus, movies, replays and the app's own pages show on a large virtual screen in front of the player; while the player
drives, the race is drawn in stereo from the tracked head, with the race's display on a panel in front of the car.

Only builds with `GT3R_XR` have it: the Meta Quest app (`-Pquest`, [android/README.md](../android/README.md)) and a
desktop build configured with `-DGT3R_XR=ON`. Every other build compiles none of it.

## Status

| | |
|---|---|
| Linux + Monado's simulated headset | ✅ the session starts, frames go out, the menus and the app's pages show on the screen (pictures of the canvas: Settings, the opening movie); stereo races in the chase view, the in-car view (the mirror and the display on the panel) and at night (Route 5: the lamps' glows in each eye), the eyes offset from each other; the own car's body round the in-car view sits the player in the car's driver's seat (the NSX-R: on the right) |
| macOS (a build with `GT3R_XR=ON`) | ✅ compiles; with no OpenXR runtime it opens the normal window (its pictures are the flat build's, byte for byte) |
| Meta Quest 3 / 3S / Pro / 2 (`-Pquest`) | ⚠️ the APK builds (debug, and release signed with the phone APK's key; the release's `gt4r-quest.apk`, job `package-quest`); not run on a headset. Nothing here is checked on a Quest: the frame rate, the controllers, the pointer, the system's menu over the game |
| PC VR (Quest Link, SteamVR) | ⚠️ the release's `gt4r-windows-x64-vr.zip` (job `package`, with `openxr_loader.dll`), not yet built on Windows; the engine's runtime choice and the window's mirror are in, untried here |

## How it works

```
App::init      xr_start (app_xr.cpp): the 3D device made through OpenXR when a headset is there ([vr] enabled), its
               session (canvas, eyes, controllers); without one the window as always
App::iterate   xr_begin_frame: the session's events (the system's menu over the game pauses a race the player
               drives: SDL_EVENT_DID_ENTER_BACKGROUND to the top screen), the wait for the headset's frame, the
               step by its display times
               xr_input: the controllers into the engine's virtual pad, the right controller's pointer into the
               page as the mouse's events (not while driving), Recenter view
               the frame drawn into the session's canvas (1280 x 720: the 640 x 360 UI at 2x)
               RaceScreen: render_eyes in a stereo frame (below)
               SDL_RenderPresent, then the session's end_frame: the canvas (or the eyes and the panel) to the headset
```

**The race in stereo** (`race_vr.cpp`; the race's hook in `screen_race.cpp` `Race::render`). Only the car's own views while
the player drives (`Race::stereo_seat`): the opening shots, the trackside and pit cameras, replays, demonstrations, B-spec, the
menus' scene, a test's end and the split screen stay on the virtual screen. The headset's seated space (its origin at the head
when centred) is placed at the race camera's eye:

| View | Placement |
|---|---|
| In-car (mount 0) | the car's camera point with the car's attitude (`[vr] level_horizon`: its heading only); the seat moved from there by `seat_side`, `seat_forward`, `seat_height`; looking back turns it half round |
| Roof, bumper | the camera's point with the car's attitude |
| Chase, far chase | the camera's eye, level, along the camera's lagged heading (looking back included) |

The camera's own nod, shake and sideways look are left out: the head does them. The pad's right stick look is off in a
headset (`vr::head_looks`). Each eye's view is that placement times its tracked pose; its lens the eye's own off-centre
frustum.

`RaceWorld::render_eyes` (`race_world.cpp`) collects the scene once from the head (`collect`: the course's lists and fades,
the cars and their levels of detail, the smoke, the cars' reflection picture) and draws each eye into its canvas with
`draw_view`, the flat view's own second half: the road's reflection buffer, the sun's glare and the lamps' glows per eye
(their frustum), the picture's grade. The sky and the sun's disc are drawn round each eye, not round the head (a dome tens of
metres across centred between the eyes would stand that near in stereo). The cars' reflections are looked up from the race
camera's view (`DrawOptions::reflection_view`), so turning the head does not turn them. The canvas, cleared transparent,
then takes the race's display and the in-car view's mirror as on a screen: the engine shows it as the panel in front.

`[vr] car_body = on` draws the own car's body round the in-car view; `seat_side = car` sits the player where the car's
driver sits (its figure's seat in the car file's cockpit record, `race::posture::Cockpit::seat`, +x to the right; the
NSX-R's is on the right). GT4's car bodies have a plain dark cabin, so the body is off by default, as in the engine's game.
The engine's `car_windows` (its game's tinted glass) has nothing to do here.

## Settings ([vr] in config.ini, Settings > Display in a headset build)

The engine's `xr::Settings` (its docs/dev/VR.md "Settings"): Races (on the screen / in 3D), In-car view (turns with the car /
level horizon), Your car in the in-car view, Steering (thumbstick / both grips as a wheel) and its rotation, the screen's and
the race display's size and distance, In-car seat (middle, left, right, the car's driver's) and how far aside, the seat's
height and reach, the refresh rate, the eyes' resolution, Recenter view (F9). On a PC with more than one kind of VR software,
Headset software. In a headset the display's rows a headset decides (Screen, VSync, Frame rate, Gamma, 3D resolution, Dynamic
resolution) show "The headset's".

## Building

Quest: `android/fetch-sdl.sh --xr`, then `./gradlew assembleDebug -Pquest` in `android/` (app-quest-debug.apk; the same
application id as the phone app: `org.gt4revisited.gt4r`, and a debug build's `org.gt4revisited.gt4r.debug`).

Desktop: `cmake -S . -B build-xr -DGT3R_XR=ON` (SDL's development branch at the engine's pinned commit; a local copy with
`-DFETCHCONTENT_SOURCE_DIR_SDL3=<path>`). Windows also gets the Khronos OpenXR loader next to `gt4r.exe`, and the package
(`cmake --install`) carries it.

Release packages (`.github/workflows/build.yml`, docs/BUILDING.md "Making a release"): `gt4r-quest.apk` and
`gt4r-windows-x64-vr.zip`. A VR build's **Download update** fetches its own package, never the flat one.

## Testing without a headset

Linux with Monado's null compositor and simulated headset (the engine's docs/dev/VR.md "Testing"), e.g. in a VM:

```bash
sleep 900 | XRT_COMPOSITOR_NULL=1 SIMULATED_ENABLE=1 monado-service &
```
```bash
XR_RUNTIME_JSON=/usr/share/openxr/1/openxr_monado.json GT3R_VR_EYE_SIZE=720 GT4R_DATA_DIR=<scratch> SDL_AUDIODRIVER=dummy GT4R_RACE="course=r_apricot,car=nsx_r_02,view=in,drive=auto,skip=600" xvfb-run -a build-xr/gt4r <gt4 iso> --screen race --shot-frame 300 --shot out.png
```

In a stereo frame `--shot` writes the canvas (the display over transparency) and `out-eyes.png`, the two eyes side by side.

## Open items

- Everything on a real headset: the Quest's frame rate (the race draws each eye through `View3D::draw`, with its composite
  pass, where the engine's game renders straight into the eye's image), the controllers' layout and the pointer on the game's
  menus, the system's menu pausing a race.
- The race's pause and results pages take no pointer (the engine's race menus aim the ray at the panel; ours read the pad).
- The online race's car names are placed for the flat view.

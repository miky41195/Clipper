# Clipper

[![CI](https://github.com/miky41195/clipper/actions/workflows/ci.yml/badge.svg)](https://github.com/miky41195/clipper/actions/workflows/ci.yml)

Lightweight clip recorder for Windows 11. It always keeps the last N seconds of your screen ready (replay buffer) and saves them with a hotkey; it can also record manually. Every clip stores audio on separate tracks: Mix, Microphone, and one track for each app that is playing sound. In the gallery you can mute or lower any track and export the clip without re-encoding the video.

## Download

Grab `Clipper-Setup-<version>.exe` from the [latest release](https://github.com/miky41195/clipper/releases/latest) and run it. Requires Windows 11 (x64).

## Usage

- Run `clipper.exe` (or `build\bin\clipper.exe` if you built it yourself): a dot appears in the tray (green = buffer active, red = recording, grey = paused). `clipper.exe --show` opens the window right away; if Clipper is already running, launching it again opens the window.
- **Alt+F10**: save the last few seconds.
- **Alt+Shift+F9**: start / stop recording.
- **Alt+G**: open the gallery.
- Click the tray icon: save, record, pause, gallery, settings, folder, exit.

## Window

- **Gallery**: clips grouped by day, search by name or game. Right-click a clip: rename, show in folder, copy the file to the clipboard (to paste into Discord), move to Recycle Bin.
- **Player**: click the video or press Space to play/pause, ←/→ arrows for ±5 s. The "Audio tracks" panel mutes or adjusts each track while you listen. The Mix track starts muted because it duplicates the others.
- **Export**: creates `<name>_export.mp4` with the video copied losslessly and the selected tracks mixed into one; optionally keeps the separate tracks too.
- **Settings**: capture (monitor, resolution, fps, codec, quality), replay (duration, RAM→disk threshold), audio (microphone, apps never to record), hotkeys (click and press), saving (folder, format, file name), general (start with Windows, sounds, language, color). The **Estimate** card shows in real time how large clips and the buffer will be. "Apply" restarts capture only when needed.

Settings are also stored in `%APPDATA%\Clipper\settings.json`.

## Installer (for sharing with friends)

```
make_installer.cmd          :: -> dist\Clipper-Setup-1.0.0.exe (~38 MB)
make_installer.cmd 1.2.0    :: -> dist\Clipper-Setup-1.2.0.exe
```

Pushing a tag like `v1.2.0` runs [release.yml](.github/workflows/release.yml), which builds the installer and publishes it as a GitHub release.

## Build and test

```
build.cmd                                   :: build
build\bin\clipper_tests.exe                 :: all tests (GPU, screen; no sound)
build\bin\clipper_tests.exe -tse=gpu,capture,desktop   :: unit tests only
set CLIPPER_AUDIO_TESTS=1                   :: enable the audio capture test (plays a sound!)
build\bin\clipper.exe --bench 30            :: measure CPU/RAM and save a test clip
build\bin\clipper.exe --render-ui settings.capture out.png 1.5   :: preview a page
```

CI runs on every pull request and push to `main`: it builds with MSVC and runs `ctest`, which covers the unit suites only (GitHub runners have no GPU, display or audio device). Run the GPU, capture and desktop tests locally before merging changes in those areas.

Log: `%LOCALAPPDATA%\Clipper\logs\clipper.log`. Gallery thumbnails and index: `%LOCALAPPDATA%\Clipper\thumbs`.

## License

Clipper is released under the [MIT License](LICENSE). It uses FFmpeg (LGPL v3, loaded as separate DLLs), Dear ImGui and nlohmann/json; see [installer/THIRD-PARTY.txt](installer/THIRD-PARTY.txt) for details and sources.

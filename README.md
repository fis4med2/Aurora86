# AURORA/86

[Português](README.pt-BR.md) · [Download v1.1.0](https://github.com/fis4med2/Aurora86/releases/tag/v1.1.0)

A fictional 1986 personal computer, built in **C++20 and Qt 6 for Linux**. AURORA has its own boot sequence, desktop, Unix-inspired shell, shared virtual disk and native applications. The interface and simulated documents are currently in Brazilian Portuguese.

![AURORA/86 desktop](AURORA86-desktop.png)

## What's new in 1.1

- **Your own wallpaper:** import an image from Linux, preview it, choose fill/fit/stretch/center, or apply a four-tone retro filter. The simulator stores its own reduced copy.
- **Readable text:** embedded DejaVu Sans Mono, smooth text and persistent 14/16/18/20 px sizes. Default: 16 px. Larger controls and windows.
- **Three new games:** Prisma, Jardim Orbital and Eco, alongside Circuito and Vigília Orbital. Five playable games in the new Games Room.

![Wallpaper controls](AURORA86-papel-de-parede.png)
![Games Room](AURORA86-jogos.png)
![Prisma laser puzzle](AURORA86-prisma.png)

## Run

Download the executable from [Releases](https://github.com/fis4med2/Aurora86/releases). This AppImage is **x86_64 and requires glibc 2.44 or newer**, an X11/Wayland session and host graphics drivers. Qt, WebEngine/Chromium, fonts and audio libraries are included. It is not compatible with older distributions without rebuilding against an older baseline.

```sh
chmod +x AURORA86-x86_64.AppImage
./AURORA86-x86_64.AppImage
```

Without FUSE:

```sh
./AURORA86-x86_64.AppImage --appimage-extract-and-run
```

**F11** toggles fullscreen. Enter/Space skips boot. Drag window title bars; resize from the bottom-right corner. The Windows menu restores minimized applications.

Change wallpaper through **Controle → Papel de parede**, or right-click the desktop. Change font size through **Controle → Sistema e letras**. Imported images are copied into the virtual disk; the original Linux file is unchanged.

## Applications

- **ASTER terminal:** `help`, `ls -l`, `cd`, `pwd`, `cat`, `echo` with `>`/`>>`, `mkdir`, `touch`, `rm -r`, `mv`, `cp`, `chmod`, `stat`, `df`, `free`, `ps`, `kill`, `date`, `uname`, `whoami`, `clear`. Quoted paths, history and Tab completion. Launch games with `prisma`, `jardim`, `eco`, `snake`, `blocks`, or `games`. `exit` closes the terminal; `shutdown` closes the simulator.
- **Disco 0:** create, open, delete and move/rename virtual files and directories.
- **Escritor:** UTF-8 editor with Ctrl+S and unsaved-change/overwrite prompts.
- **Calculator, image viewer, system monitor and settings.**
- **Correio de Rede:** real HTTP/HTTPS browsing through Chromium, navigation controls and HTML saving to the virtual disk. Browser file access, uploads, host downloads and device permissions are disabled. Chromium sandboxing stays enabled.

## Games

| Game | How to play |
| --- | --- |
| Prisma | Rotate mirrors to route a laser to its receiver. Six levels; puzzle state persists. |
| Jardim Orbital | Plan cross-shaped watering moves to restore an orbital garden. Eight puzzles; progress persists. |
| Eco | Watch and listen, then repeat the pad sequence using mouse or keyboard. Persistent best score. |
| Circuito | Route a growing signal using arrows/WASD without crossing its trail. Space pauses; Enter restarts. |
| Vigília Orbital | Move with arrows/WASD and shoot with Space. P pauses; Enter restarts. Three shields and persistent best score. |

## Persistence and boundaries

The A16 models 8 MHz, **4096 KiB simulated RAM** and a **2048 KiB virtual disk**. All virtual files, permissions, settings, imported wallpaper and game progress/records are stored atomically in `vfs.json` under `~/.local/share/Aurora/AURORA86/` or the corresponding XDG data directory. `--state-dir PATH` selects a separate computer. Existing 1.0 states remain usable.

`/bin`, `/sys` and `/etc` are protected; `/home/guest` and `/tmp` are writable. Virtual paths never become Linux paths and the shell does not execute host commands. The wallpaper picker is an explicit, read-only import bridge. The clock is simulated. Invalid state is preserved rather than overwritten. A lock prevents two sessions from overwriting the same disk.

This is an interactive environment simulator, not CPU instruction emulation or a complete Unix. RAM quotas are simulated; actual Chromium memory use depends on websites. Browser sessions are in memory, DRM is not bundled, and some sites may require disabled capabilities. Active arcade rounds restart; puzzle progress and best scores persist.

## Build and test

Requires CMake ≥3.21, a C++20 compiler and Qt ≥6.8 with Core, Widgets, Multimedia, WebEngineWidgets and Test.

```sh
cmake -S . -B work/build -DCMAKE_BUILD_TYPE=Release
cmake --build work/build -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir work/build --output-on-failure
./work/build/aurora86
bash packaging/build-appimage.sh
```

The AppImage script also uses Python 3, curl, ldd and readelf. Official AppImage tools are downloaded with pinned SHA-256 hashes. Host glibc and graphics drivers remain external; the script records compatibility in `COMPATIBILIDADE.txt`. Rebuild dependencies on an older distribution for a wider baseline.

`--self-test` checks applications without network access. `--browser-check https://example.com` checks real Chromium navigation. `AURORA_NO_AUDIO=1` disables audio; offscreen tests do so automatically. Wallpaper imports are reduced to at most 960×640 and respect virtual disk capacity.

Core files live in `src/`, regression tests in `tests/`, and distribution metadata/scripts in `packaging/`. Main downloads and screenshots are also placed directly in the project folder. AppImages are release assets rather than Git objects because they exceed GitHub's normal file size limit.

## Distribution status and license

GitHub hosts source, screenshots and AppImage releases. **This project has not been submitted to Flathub.** Its [current policy](https://docs.flathub.org/docs/for-app-authors/requirements#generative-ai-policy) prohibits AI-generated/assisted submission manifests and AI-agent submission interactions. A human maintainer must independently handle any compliant submission and review the remaining requirements, including English UI localization and development history.

Most original application code, documentation and packaging were generated or edited with AI assistance, then built and tested. Third-party libraries and fonts retain their own licenses. Project code: **GPL-3.0-or-later**. Font licenses are in `assets/`; bundled dependency notices are inside the extracted AppImage under `usr/share/licenses`.

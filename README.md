# AURORA/86 1.2.0

[Português](README.pt-BR.md) · [Downloads](https://github.com/fis4med2/Aurora86/releases)

A fictional 1986 personal computer, built in **C++20 and Qt 6**. AURORA has its own boot sequence, desktop, Unix-inspired shell, shared virtual disk, package manager, Lua runtime and native applications. The interface is in English.

![AURORA/86 desktop](AURORA86-desktop.png)

## What's new in 1.2

- **English interface:** menus, applications, help text and simulated system files now use English.
- **auroraSYNC:** sign in as Guest, customize name, login, note, message, avatar and background, set an optional password, and lock the session with Ctrl+Alt+L.
- **Packages:** an offline repository with 16 packages and 23 utilities, with dependencies and simulated disk use. Install from the Package Center or with `aurora`.
- **Lua 5.4.8:** Lua Studio installs and edits portable mods from a host folder. Scripts run inside the simulated computer.
- **Windows builds:** the workflow produces an installer and a portable ZIP once validation finishes.

![Wallpaper controls](AURORA86-papel-de-parede.png)
![Games Room](AURORA86-jogos.png)
![Prism laser puzzle](AURORA86-prisma.png)

## Run

Download the executable from [Releases](https://github.com/fis4med2/Aurora86/releases). The Linux AppImage is **x86_64 and requires glibc 2.44 or newer**, an X11/Wayland session and host graphics drivers. Qt, WebEngine/Chromium, fonts and audio libraries are included. Older distributions need a rebuild against an older baseline.

```sh
chmod +x AURORA86-x86_64.AppImage
./AURORA86-x86_64.AppImage
```

Without FUSE:

```sh
./AURORA86-x86_64.AppImage --appimage-extract-and-run
```

The Windows build is still being validated. When the [workflow](.github/workflows/windows.yml) completes, it produces two assets:

- `AURORA86-Setup-x64.exe`: installer for Windows 10/11 x64, no administrator rights required.
- `AURORA86-Windows-x86_64.zip`: extract the whole folder and open `aurora86.exe`. Keep the DLLs, plugins and resources together.

First login: **Guest, no password → Sign in**. Enter/Space skips boot. **F11** toggles fullscreen. Drag window title bars to move them and resize from the bottom-right corner. The Windows menu restores minimized applications.

For wallpaper, use **AURORA / 86 → Wallpaper...** or right-click the desktop. Display presets, text size, themes, audio and clock live in **AURORA / 86 → Control Panel**. The auroraSYNC profile is under **AURORA / 86 → auroraSYNC profile...**. Imported images are copied into the virtual disk; the original Linux file is unchanged.

## Applications

- **ASTER terminal:** `help`, `ls -l`, `cd`, `pwd`, `cat`, `echo` with `>`/`>>`, `mkdir`, `touch`, `rm -r`, `mv`, `cp`, `chmod`, `stat`, `df`, `free`, `ps`, `kill`, `date`, `uname`, `whoami`, `clear`. Quoted paths, history and Tab completion. Games open with `prism`, `garden`, `eco`, `snake`, `blocks` or `games`. `exit` closes the terminal; `shutdown` closes the simulator.
- **Disk 0:** create, open, delete and move/rename virtual files and directories.
- **Writer:** UTF-8 editor with Ctrl+S and unsaved-change/overwrite prompts.
- **Control Panel, calculator, image viewer and system monitor.**
- **Package Center:** browse, search, install and remove the offline packages.
- **Lua Studio:** list, run, edit, install and remove Lua programs.
- **Network Mail:** real HTTP/HTTPS browsing through Chromium, navigation controls and HTML saving to the virtual disk. Browser file access, uploads, host downloads and device permissions are disabled. Chromium sandboxing stays enabled.

## Packages

The catalog is offline and uses the virtual disk. These are simulated tools with virtual files and processes, not host programs.

```text
aurora --list
aurora -i fastfetch htop tree grep coreutils manuals
fastfetch
htop
man grep
aurora -l
```

Other tools include neofetch, cowsay, figlet, cal, nano and less. `aurora -s TEXT` searches, `aurora --info NAME` shows details and `aurora -r NAME` removes a package after checking dependencies. htop shows live simulated processes with F2/F3/F9/F10 controls.

## Lua programs and mods

Open **Lua Studio** from Applications or enter `mods`. Its `mods/` folder is created automatically when possible; **Choose folder** selects another host folder. Drop a UTF-8 `.lua` file there, then install and run it:

```text
aurora -i hello
hello
```

Examples live in [`mods/`](mods/): `hello`, `notepad` and `theme-switcher`. Installation copies scripts into the virtual disk. Lua Studio can also run, edit or remove installed programs. Run a virtual file directly with `lua /home/guest/docs/program.lua`.

Save this as `my-app.lua`; install with `aurora -i my-app`, then run `my-app`:

```lua
-- @name my-app
local n = 0
aurora.window {
    title = "My program", text = "Click a button",
    buttons = {
        {label = "Count", action = function()
            n = n + 1
            aurora.text("Count: " .. n)
        end},
        {label = "Reset", action = function()
            n = 0
            aurora.text("Count: 0")
        end}
    }
}
```

Lua API: `aurora.read/write/list/mkdir` and `aurora.theme/setting`. Limits: **1 MiB heap**, an instruction budget and **64 KiB output**. There is no host io/os, native module or code loader; some functions are restricted. Mods are portable and need no DLLs. Changes to the C++ core require an SDK and a rebuild.

## Games

| Game | How to play |
| --- | --- |
| Prism | Rotate mirrors to route a laser to its receiver. Six levels; puzzle state persists. |
| Orbital Garden | Plan cross-shaped watering moves to restore an orbital garden. Eight puzzles; progress persists. |
| Echo | Watch and listen, then repeat the pad sequence using mouse or keyboard. Persistent best score. |
| Circuit | Route a growing signal using arrows/WASD without crossing its trail. Space pauses; Enter restarts. |
| Orbital Watch | Move with arrows/WASD and shoot with Space. P pauses; Enter restarts. Three shields and persistent best score. |

## Persistence and boundaries

The A16 models 8 MHz, **4096 KiB simulated RAM** and a **2048 KiB virtual disk**. Virtual files, permissions, settings, the auroraSYNC profile, installed packages and mods, imported wallpaper and game progress are stored atomically in `vfs.json` under `~/.local/share/Aurora/AURORA86/` or the corresponding XDG data directory. `--state-dir PATH` selects a separate computer. Existing 1.1 states remain usable.

`/bin`, `/sys` and `/etc` are protected; `/home/guest` and `/tmp` are writable. Virtual paths never become Linux paths, and the shell and Lua do not execute host commands. The wallpaper picker is an explicit, read-only import bridge. The clock is simulated. Invalid state is preserved rather than overwritten, and a lock prevents two sessions from writing to the same disk.

This is an interactive environment simulator, not CPU instruction emulation or a complete Unix. RAM quotas are simulated; actual Chromium memory use depends on websites. Browser sessions stay in memory, DRM is not bundled, and some sites may need capabilities that are disabled. Active arcade rounds restart; puzzle progress and best scores persist.

## Build and test

Requires CMake ≥3.21, a C++20 compiler and Qt ≥6.8 with Core, Widgets, Network, Multimedia, WebEngineWidgets and Test. The Lua source is included in `third_party/`.

```sh
cmake -S . -B work/build -DCMAKE_BUILD_TYPE=Release
cmake --build work/build -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir work/build --output-on-failure
./work/build/aurora86
bash packaging/build-appimage.sh
```

The AppImage script also uses Python 3, curl, ldd and readelf. Official AppImage tools are downloaded with pinned SHA-256 hashes. Host glibc and graphics drivers remain external; the script records compatibility in `COMPATIBILIDADE.txt`. Rebuild dependencies on an older distribution for a wider baseline.

`--self-test` checks applications without network access. `--browser-check https://example.com` checks real Chromium navigation. `AURORA_NO_AUDIO=1` disables audio; offscreen tests do this automatically. Wallpaper imports are reduced to at most 960×640 and respect virtual disk capacity.

Core files live in `src/`, regression tests in `tests/`, and distribution metadata/scripts in `packaging/`. Main downloads and screenshots are also placed directly in the project folder. AppImages are release assets rather than Git objects because they exceed GitHub's normal file size limit.

## License

Project code is **GPL-3.0-or-later**. Lua is MIT. Third-party libraries and fonts keep their own licenses; font licenses are in `assets/`, and dependency notices are under `usr/share/licenses` in the extracted AppImage.

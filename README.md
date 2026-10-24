# AURORA/86 1.2.0

[Português](README.pt-BR.md) · [Downloads](https://github.com/fis4med2/Aurora86/releases)

A fictional 1986 computer: **English OS**, Unix-style desktop, packages, five games and embedded **Lua 5.4.8**. C++20/Qt 6 core. **Users need no C++, Qt or Lua installation.**

![Desktop](AURORA86-desktop.png)

## Download and open

**Windows validation is pending.** The [workflow](.github/workflows/windows.yml) produces these release assets when available:

- `AURORA86-Setup-x64.exe`: open and follow setup. Designed for Windows 10/11 x64, installing in your account without administrator rights.
- `AURORA86-Windows-x86_64.zip`: extract everything, then open `aurora86.exe`. Keep its DLLs, plugins and resources together.

**Linux:** download `AURORA86-x86_64.AppImage`. Requires **x86_64, glibc ≥2.44**, X11/Wayland and host graphics drivers. Runtime dependencies are included; older distributions need a rebuild.

```sh
chmod +x AURORA86-x86_64.AppImage
./AURORA86-x86_64.AppImage
```

Without FUSE, add `--appimage-extract-and-run`.

## auroraSYNC and customization

First login: **Guest, no password → Sign in**. **Control → auroraSYNC** customizes name, login, note, message, avatar, background and optional password. **Ctrl+Alt+L** locks the session. Passwords use salted PBKDF2-SHA256 verifiers. Identity is local, not cloud/host authentication; the home stays **`/home/guest`** after renaming.

**Control → Display:** Auto or fixed 960×640–1600×900, with scrollbars. **F11:** fullscreen. **System and text:** 14/16/18/20 px letters, themes, audio, clock. **Wallpaper:** image import, fit modes and retro filter. Host images/display stay unchanged.

![Login](AURORA86-login.png)

## Packages and apps

Offline catalog: **16 packages / 23 utilities**, with dependencies and virtual disk use. These simulated tools use virtual files/processes, not host programs.

```text
aurora --list
aurora -i fastfetch htop tree grep coreutils manuals
fastfetch
htop
man grep
aurora -l
```

More tools include neofetch, cowsay, figlet, nano and less. `aurora -s TEXT` searches; `--info NAME` shows details; `-r NAME` removes. Package Center provides buttons. htop shows live processes with F2/F3/F9/F10 controls.

Apps include Terminal, files, Writer, calculator, images and monitors. Chromium browses real HTTP/HTTPS and saves HTML virtually; host uploads/downloads/devices are disabled. Sandbox enabled; no DRM.

![Packages](AURORA86-packages.png)

## Lua programs and mods

Open **Lua Studio** from Applications or enter `mods`. Its displayed `mods/` folder is created automatically when possible; **Choose folder** selects another host folder. Drop a UTF-8 `.lua` file there, then install/run:

```text
aurora -i hello
hello
```

Examples in [`mods/`](mods/): `hello`, `notepad`, `theme-switcher`. Installation copies scripts into the virtual disk. Lua Studio can run/edit/remove them or create a new program. Run a virtual file with `lua /home/guest/docs/program.lua`.

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

Lua API: `aurora.read/write/list/mkdir`, `aurora.theme/setting`. Limits: **1 MiB heap**, instruction budget and **64 KiB output**. No host io/os, native modules or code loader; some functions are restricted. Mods are portable without DLLs. **C++ core changes require an SDK/rebuild.**

![Lua Studio](AURORA86-lua.png)

## Persistence, games and development

A16: 8 MHz, **4096 KiB simulated RAM**, **2048 KiB disk**. State persists in `vfs.json` in your application-data folder; Linux usually uses `~/.local/share/Aurora/AURORA86/`. `--state-dir PATH` selects another station. Old documents survive. Images/mod folders are explicit imports; shell/Lua never execute host commands. CPU instructions are not emulated; Chromium uses real RAM.

Games: Prism, Orbital Garden, Echo, Circuit and Orbital Watch. Puzzle progress and records persist.

Developers need CMake ≥3.21, C99/C++20 and Qt ≥6.8: Core, Widgets, Network, Multimedia, WebEngineWidgets, Test. Lua source is included.

```sh
cmake -S . -B work/build -DCMAKE_BUILD_TYPE=Release
cmake --build work/build -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir work/build --output-on-failure
```

Packaging: `packaging/`. **GPL-3.0-or-later**; Lua MIT and third-party notices included. Most code/docs/packaging used AI assistance and were built/tested; Windows status is separate.

**Not on Flathub:** its [policy](https://docs.flathub.org/docs/for-app-authors/requirements#generative-ai-policy) prohibits AI-assisted manifests/agent submissions; a human must handle any compliant submission.

# AURORA/86 1.2.0

[English](README.md) · [Downloads](https://github.com/fis4med2/Aurora86/releases)

Um computador fictício de 1986: **OS em inglês**, pacotes, cinco jogos e **Lua 5.4.8**. **Sem instalar C++, Qt ou Lua para usar.**

![Desktop](AURORA86-desktop.png)

## Baixar e abrir

**Validação Windows pendente.** O [workflow](.github/workflows/windows.yml) produz os pacotes para as releases quando disponíveis:

- `AURORA86-Setup-x64.exe`: abra e siga a instalação. Preparado para Windows 10/11 x64, na sua conta, sem exigir administrador.
- `AURORA86-Windows-x86_64.zip`: extraia tudo e abra `aurora86.exe`. Mantenha as DLLs, plugins e recursos juntos.

**Linux:** baixe `AURORA86-x86_64.AppImage`. Exige **x86_64, glibc ≥2.44**, X11/Wayland e drivers gráficos do host. Dependências estão incluídas; distribuições antigas precisam de outra compilação.

```sh
chmod +x AURORA86-x86_64.AppImage
./AURORA86-x86_64.AppImage
```

Sem FUSE, acrescente `--appimage-extract-and-run`.

## auroraSYNC e personalização

Primeiro login: **Guest, sem senha → Sign in**. **Control → auroraSYNC** ajusta nome, login, nota, mensagem, avatar, fundo e senha opcional. **Ctrl+Alt+L** bloqueia a sessão. O verificador usa PBKDF2-SHA256 com salt. A identidade é local, não nuvem/autenticação do host; a pasta continua **`/home/guest`** após renomear.

**Control → Display:** Auto ou 960×640–1600×900 fixos, com rolagem. **F11:** tela cheia. **System and text:** letras de 14/16/18/20 px, temas, sons e relógio. **Wallpaper:** importar imagem, encaixes e filtro retrô. Imagens/monitor reais ficam iguais.

![Login](AURORA86-login.png)

## Pacotes e aplicativos

Catálogo offline: **16 pacotes / 23 utilitários**, com dependências e espaço virtual. Trabalham com arquivos/processos simulados, não programas do host.

```text
aurora --list
aurora -i fastfetch htop tree grep coreutils manuals
fastfetch
htop
man grep
aurora -l
```

Mais: cowsay, nano e less. `aurora -s TEXTO` pesquisa; `--info NOME` mostra detalhes; `-r NOME` remove. Package Center oferece botões. htop mostra processos atuais, com controles F2/F3/F9/F10.

Aplicativos: Terminal, arquivos, Writer, calculadora, imagens e monitores. Chromium acessa HTTP/HTTPS reais e grava HTML virtualmente; uploads/downloads do host e dispositivos desativados. Proteção ativa; sem DRM.

![Pacotes](AURORA86-packages.png)

## Programas e mods Lua

Abra **Lua Studio** em Applications ou digite `mods`. A pasta `mods/` mostrada é criada automaticamente quando possível; **Choose folder** escolhe outra pasta real. Coloque nela um `.lua` em UTF-8 e instale/execute:

```text
aurora -i hello
hello
```

Exemplos em [`mods/`](mods/): `hello`, `notepad`, `theme-switcher`. A instalação copia scripts para o disco virtual. Lua Studio permite executar/editar/remover ou criar um programa. Execute arquivo virtual com `lua /home/guest/docs/program.lua`.

Grave como `my-app.lua`, instale com `aurora -i my-app` e execute `my-app`:

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

API Lua: `aurora.read/write/list/mkdir` e `aurora.theme/setting`. Limites: **heap de 1 MiB**, orçamento de instruções e **saída de 64 KiB**. Sem io/os do host, módulos nativos ou carregador de código; algumas funções são restritas. Mods portáteis, sem DLLs. **Mudar o núcleo C++ exige SDK/recompilar.**

![Lua Studio](AURORA86-lua.png)

## Persistência, jogos e desenvolvimento

A16: 8 MHz, **4096 KiB de RAM simulada**, **2048 KiB de disco**. Estado persistente em `vfs.json`, na pasta de dados da conta; Linux geralmente usa `~/.local/share/Aurora/AURORA86/`. `--state-dir CAMINHO` escolhe outra estação. Documentos antigos ficam preservados. Imagens/mods são importações explícitas; terminal/Lua não executam comandos do host. CPU não é emulada; Chromium usa RAM real.

Jogos: Prism, Orbital Garden, Echo, Circuit e Orbital Watch. Puzzles e recordes persistem.

Para desenvolver: CMake ≥3.21, C99/C++20 e Qt ≥6.8 com Widgets, Network, Multimedia, WebEngineWidgets e Test. Lua incluído.

```sh
cmake -S . -B work/build -DCMAKE_BUILD_TYPE=Release
cmake --build work/build -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir work/build --output-on-failure
```

Empacotamento: `packaging/`. **GPL-3.0-or-later**; Lua MIT e avisos de terceiros incluídos. A maioria do código/docs/empacotamento usou IA e foi compilada/testada; Windows tem situação separada.

**Não está no Flathub:** a [política](https://docs.flathub.org/docs/for-app-authors/requirements#generative-ai-policy) proíbe manifestos assistidos por IA/envio por agentes; um humano precisa conduzir uma submissão compatível.

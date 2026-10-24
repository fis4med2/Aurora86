# AURORA/86 1.2.0

[English](README.md) · [Downloads](https://github.com/fis4med2/Aurora86/releases)

Computador pessoal fictício de 1986, escrito em **C++20 e Qt 6**. O AURORA tem boot próprio, desktop, terminal inspirado no Unix, disco virtual compartilhado, gerenciador de pacotes, runtime Lua e aplicativos nativos. A interface está em inglês.

![Desktop AURORA/86](AURORA86-desktop.png)

## Novidades da versão 1.2

- **Interface em inglês:** menus, aplicativos, textos de ajuda e arquivos do sistema simulado agora usam inglês.
- **auroraSYNC:** entre como Guest, ajuste nome, login, nota, mensagem, avatar e fundo, defina uma senha opcional e bloqueie a sessão com Ctrl+Alt+L.
- **Pacotes:** repositório offline com 16 pacotes e 23 utilitários, com dependências e espaço simulado em disco. Instale pelo Package Center ou com `aurora`.
- **Lua 5.4.8:** o Lua Studio instala e edita mods portáteis a partir de uma pasta do Linux. Os scripts rodam dentro do computador simulado.
- **Controles de tela:** escolha a resolução do desktop, ajuste o tamanho do texto e alterne temas. F11 alterna tela cheia.
- **Ferramentas da estação:** F1 abre o manual; Print Screen salva uma captura virtual e `/proc` mostra hardware e processos simulados em tempo real.

![Login auroraSYNC](AURORA86-login.png)
![Package Center](AURORA86-packages.png)
![Programas Lua](AURORA86-lua.png)
![Controle do papel de parede](AURORA86-papel-de-parede.png)
![Sala de jogos](AURORA86-jogos.png)
![Quebra-cabeça Prism](AURORA86-prisma.png)

## Executar

Baixe o AppImage em [Releases](https://github.com/fis4med2/Aurora86/releases). Para executar, não precisa instalar compilador, SDK C++ ou Lua separado. O AppImage para Linux é **x86_64 e exige glibc 2.44 ou superior**, sessão X11/Wayland e drivers gráficos do Linux. Qt, Chromium/WebEngine, fontes e bibliotecas de áudio estão incluídos. Distribuições antigas precisam de outra compilação sobre uma base mais antiga.

```sh
chmod +x AURORA86-x86_64.AppImage
./AURORA86-x86_64.AppImage
```

Sem FUSE:

```sh
./AURORA86-x86_64.AppImage --appimage-extract-and-run
```

Primeiro login: **Guest, sem senha → Sign in**. Enter/Espaço pula o boot. **F11** alterna tela cheia. Arraste a barra de título para mover janelas e redimensione pelo canto inferior direito. O menu **Windows** restaura aplicativos minimizados. **F1** abre o manual da estação, com guias de teclado, terminal e Lua.

Para trocar o papel de parede, use **AURORA / 86 → Wallpaper...** ou clique com o botão direito no desktop. Presets de tela, tamanho do texto, temas, áudio e relógio ficam em **AURORA / 86 → Control Panel**. O perfil auroraSYNC está em **AURORA / 86 → auroraSYNC profile...**. A importação copia a imagem para o disco virtual e não altera o arquivo original no Linux.

## Aplicativos

- **Terminal ASTER:** `help`, `ls -l`, `cd`, `pwd`, `cat`, `echo` com `>`/`>>`, `mkdir`, `touch`, `rm -r`, `mv`, `cp`, `chmod`, `stat`, `df`, `free`, `ps`, `kill`, `date`, `uname`, `whoami`, `clear`. Aspas, histórico e Tab para completar caminhos. Jogos abrem com `prism`, `garden`, `eco`, `snake`, `blocks` ou `games`. `exit` fecha o terminal; `shutdown` encerra o simulador.
- **Disk 0:** criação, abertura, exclusão e movimentação de arquivos e pastas virtuais.
- **Writer:** edição UTF-8, Ctrl+S e confirmação de alterações não gravadas ou substituição.
- **Control Panel, calculadora e monitor do sistema.**
- **Atelier:** abre imagens virtuais. **Print Screen** ou **File → Capture screen** salva a janela do simulador em `/home/guest/images/snap-NNNN.png`, preserva capturas existentes e abre a imagem no Atelier. **Export to Linux...** grava a imagem selecionada no destino que você escolher.
- **Package Center:** navegar, pesquisar, instalar e remover os pacotes offline.
- **Lua Studio:** listar, executar, editar, instalar e remover programas Lua.
- **Network Mail:** HTTP/HTTPS reais pelo Chromium, navegação e gravação de HTML no disco virtual. Acesso a arquivos locais, uploads, downloads para o Linux e permissões de dispositivos ficam desativados. A proteção do Chromium permanece ativa.

## Pacotes

O catálogo é offline e usa o disco virtual. São ferramentas simuladas, com arquivos e processos virtuais, não programas do Linux.

```text
aurora --list
aurora -i fastfetch htop tree grep coreutils manuals
fastfetch
htop
man grep
aurora -l
```

Outros utilitários incluem neofetch, cowsay, figlet, cal, nano e less. `aurora -s TEXTO` pesquisa, `aurora --info NOME` mostra detalhes e `aurora -r NOME` remove o pacote depois de conferir as dependências. O htop mostra processos simulados com controles F2/F3/F9/F10.

Inspecione a estação com `cat /proc/cpuinfo`, `cat /proc/meminfo`, `cat /proc/uptime` e `ls /proc`. Cada PID ativo tem arquivos `status` e `cmdline`. Essas entradas são somente leitura, atualizam em tempo real e não ocupam disco virtual; as pastas dos PIDs desaparecem quando o processo termina. Todas descrevem o AURORA, não o Linux.

## Programas e mods Lua

Abra o **Lua Studio** em Applications ou digite `mods`. Uma pasta `mods/` gravável é criada ao lado do AppImage quando possível, com alternativa no diretório de dados do aplicativo. O Lua Studio mostra o caminho completo; **Choose folder** escolhe outra pasta do Linux. Coloque nela um `.lua` em UTF-8 e instale:

```text
aurora -i hello
hello
```

Os exemplos ficam em [`mods/`](mods/): `hello`, `notepad`, `todo` e `theme-switcher`. O bloco de notas permite editar e salvar/recarregar texto; a lista de tarefas grava o progresso num documento virtual. A instalação copia os scripts para o disco virtual. O Lua Studio também executa, edita e remove programas instalados. Para rodar um arquivo virtual direto, use `lua /home/guest/docs/program.lua`.

Grave como `my-app.lua`, instale com `aurora -i my-app` e execute `my-app`:

```lua
-- @name my-app
local path = "/home/guest/docs/my-note.txt"
aurora.window {
    title = "My notebook",
    inputs = {
        {id = "note", label = "Note", value = aurora.read(path) or "", multiline = true}
    },
    buttons = {
        {label = "Save", action = function()
            local ok, err = aurora.write(path, aurora.input("note"))
            aurora.text(ok and "Saved to the virtual disk." or err)
        end}
    }
}
```

API Lua: `aurora.read/write/list/mkdir`, `aurora.theme/setting`, `aurora.window`, `aurora.text` e `aurora.input`. Cada janela aceita até 8 campos de texto (4096 bytes UTF-8 cada), inclusive editores de várias linhas. Leia com `aurora.input("id")` ou atualize com `aurora.input("id", "valor")`. Limites: **heap de 1 MiB**, orçamento de instruções e **saída de 64 KiB**. Não há io/os do Linux, módulos nativos ou carregador de código; algumas funções são restritas. Lua já vem integrado; os mods não exigem compilador nem runtime externo. Mudanças no núcleo C++ exigem ferramentas de desenvolvimento e recompilação.

## Jogos

| Jogo | Como jogar |
| --- | --- |
| Prism | Gire espelhos para levar o laser ao receptor. Seis níveis; estado do puzzle salvo. |
| Orbital Garden | Planeje movimentos de irrigação em cruz para restaurar o jardim. Oito puzzles; progresso salvo. |
| Echo | Observe, escute e repita a sequência nos botões com mouse ou teclado. Recorde persistente. |
| Circuit | Setas/WASD roteiam um sinal crescente sem cruzar o próprio caminho. Espaço pausa; Enter reinicia. |
| Orbital Watch | Setas/WASD movem e Espaço atira. P pausa; Enter reinicia. Três escudos e recorde salvo. |

## Persistência e limites

O A16 simula 8 MHz, **4096 KiB de RAM** e **2048 KiB de disco**. Arquivos, permissões, configurações, perfil auroraSYNC, pacotes e mods instalados, papel de parede e progresso dos jogos são gravados atomicamente em `vfs.json`, em `~/.local/share/Aurora/AURORA86/` ou no diretório XDG correspondente. `--state-dir CAMINHO` seleciona outra estação. Estados da versão 1.1 continuam utilizáveis.

`/bin`, `/sys` e `/etc` são protegidos; `/home/guest` e `/tmp` aceitam gravação. Caminhos virtuais não acessam arquivos do Linux, e o terminal e o Lua não executam comandos do host. A seleção de imagens importa explicitamente arquivos escolhidos no Linux; o Atelier exporta somente quando você escolhe um destino. O relógio é simulado. Estado inválido é preservado; uma trava impede gravações simultâneas no mesmo disco.

É um simulador de ambiente, não um emulador de instruções da CPU nem um Unix completo. As quotas de RAM são simuladas; o Chromium consome memória real conforme as páginas. Sessões do navegador ficam em memória, DRM não está incluído e alguns sites dependem de capacidades desativadas. Partidas de ação reiniciam; puzzles e recordes persistem.

## Compilar e testar

Requer CMake ≥3.21, compilador C++20 e Qt ≥6.8 com Core, Widgets, Network, Multimedia, WebEngineWidgets e Test. O código do Lua está incluído em `third_party/`.

```sh
cmake -S . -B work/build -DCMAKE_BUILD_TYPE=Release
cmake --build work/build -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir work/build --output-on-failure
./work/build/aurora86
bash packaging/build-appimage.sh
```

O empacotador usa Python 3, curl, ldd e readelf. Ferramentas oficiais AppImage são baixadas com SHA-256 fixado. glibc e drivers gráficos pertencem ao host; o requisito calculado fica em `COMPATIBILIDADE.txt`. Para ampliar a compatibilidade, reconstrua também as dependências numa distribuição mais antiga.

`--self-test` verifica os aplicativos sem rede. `--browser-check https://example.com` verifica navegação real. `AURORA_NO_AUDIO=1` desativa áudio; testes offscreen fazem isso automaticamente. Imagens importadas são reduzidas até 960×640 e respeitam a capacidade do disco virtual.

Fontes em `src/`, testes em `tests/`, metadados e empacotamento em `packaging/`. Executável e capturas também ficam diretamente na pasta principal. O AppImage é disponibilizado nos assets da release, pois ultrapassa o limite normal de arquivos Git do GitHub.

## Licença

O código do projeto é **GPL-3.0-or-later**. Lua é MIT. Bibliotecas e fontes de terceiros mantêm suas próprias licenças; licenças de fontes em `assets/` e avisos das dependências em `usr/share/licenses` dentro do AppImage extraído.

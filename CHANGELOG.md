# Changelog

Todas as mudanças relevantes do AURORA/86. O formato segue
[Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/) e o projeto usa
versionamento semântico no formato `MAJOR.MINOR.PATCH`.

## [1.4.0] — 2026-10-24

Versão "Arcade": o A16 ganha sua primeira visão em primeira pessoa, desenhada
pelo próprio processador simulado. A versão 1.3 permanece intacta.

### Adicionado

- **HELLGATE / corridor 13** (`src/hellgate.cpp`): raycaster completo em 3D.
  - Paredes texturizadas coluna a coluna (tijolo, maquinário e portões), piso e
    teto com gradiente de distância, sem nenhuma textura em disco: as imagens são
    geradas em memória a partir de código.
  - Mapa de 24 × 24 com nove salas, corredores, nove portões que abrem com `E`
    (fecham sozinhos depois de seis segundos) e um portal de saída.
  - Cinco drones com sprite billboard, perseguição quando há linha de visão,
    ataque de perto, dano, vida e recarga próprios.
  - Vida, munição, pacotes de vida e caixas de munição no mapa; HUD com vida,
    munição, drones restantes, cronômetro e melhor tempo salvo nas configurações.
  - `W A S D` mover, `Shift` correr, setas virar, `Espaço` atirar, `E` portão,
    `P` pausar, `R` reiniciar.
  - Abre por **Games → HELLGATE**, pelo Games Room ou com `hellgate` no terminal.
- **Manual da estação**: nova aba HELLGATE, com teclas e objetivo, e a referência
  de teclado (**Shift+F1**) agora lista os controles do jogo.
- **Testes**: `tests/hellgate_test.cpp`, sete casos que verificam o mapa (24 × 24,
  um início, um portão de saída, nenhuma célula isolada), a renderização do quadro,
  a colisão com paredes, o gasto de munição e o ciclo de abrir portão.

### Alterado

- Versão 1.4.0 em `CMakeLists.txt`, no rodapé, em `/etc/release`, em
  `/proc/version`, no metainfo e no instalador Windows.
- O jogo aparece no Games Room, no menu **Games** e na lista de comandos do `help`.

## [1.3.0] — 2026-10-24

Versão "Programmable": o AURORA deixa de ser só um ambiente com aplicativos e
passa a ser um computador que se programa e se automatiza.

### Adicionado

- **A16 BASIC** (`src/basic.cpp`): interpretador completo dentro do terminal.
  - Linhas numeradas, modo direto e comandos `RUN`, `LIST`, `NEW`, `CONT`, `SAVE`, `LOAD`, `SYSTEM`.
  - Instruções `PRINT` (`;`, `,`, `TAB`), `LET`, `INPUT`, `IF/THEN/ELSE`, `GOTO`,
    `GOSUB`/`RETURN`, `FOR`/`NEXT`/`STEP`, `DIM` (1 e 2 dimensões), `DATA`/`READ`/`RESTORE`,
    `REM`, `END`, `STOP`, `CLS`, `BEEP`, `RANDOMIZE`.
  - Funções numéricas e de string, erros com número de linha e limites rígidos
    (64 KiB de saída, 250 mil instruções, 2000 linhas, 4096 elementos por array).
  - `BEEP` toca o alto-falante da estação; `CLS` limpa o terminal; `SAVE`/`LOAD`
    usam o disco virtual.
  - Programa de exemplo em `/home/guest/docs/welcome.bas`.
- **ASTER com composição de comandos** (`src/shell.cpp`):
  - Pipes `cmd | cmd` para `grep`, `wc`, `head`, `tail`, `sort`, `uniq`, `base64`,
    `xxd`/`hexdump` e `cat` (que agora também lê a entrada padrão).
  - Sequências com `;` e `&&`, com código de saída em `$?`.
  - Variáveis (`$HOME`, `$USER`, `$PWD`, `$?`, `$NOME`), `export`, `unset` e `env`,
    persistidos no estado da estação.
  - `alias`/`unalias` persistentes, `history [-c] [N]` e `which`/`type`.
  - Curingas `*`, `?` e `[abc]` resolvidos contra o disco virtual.
  - `mkdir` e `touch` aceitam vários caminhos.
- **`/proc` simulado** (`src/system.cpp`): `cpuinfo`, `meminfo`, `uptime`, `version`,
  `loadavg` e `/<pid>/cmdline`, gerados a partir do estado real da estação.
- **Impressora A16**: `lpr`, `lpq` e `lprm` com fila em `/tmp/print` e cabeçalho
  de 80 colunas.
- **A16ARC** (`src/archive.cpp`): `pack`, `unpack` e `arc -l` com compressão,
  validação de nomes e limites de tamanho.
- **Interface**: barra de janelas no rodapé, protetor de tela com estrelas e logo
  quicando (configurável no Control Panel → Display), captura de tela com `PrtSc`
  (comando `screenshot`), manual com `F1` e referência de teclado com `Shift+F1`.
- **Testes**: `tests/basic_test.cpp` (15 casos) e `tests/shell_test.cpp` (14 casos)
  cobrindo BASIC, pipes, variáveis, aliases, curingas, `/proc`, impressora, A16ARC
  e a migração de estado.
- `CHANGELOG.md` e `ROADMAP.pt-BR.md`.

### Alterado

- Versão 1.3.0 em toda a estação (`uname`, `/etc/release`, Control Panel, About,
  `fastfetch`, empacotamento e metadados).
- `ls` passa a listar um item por linha quando a saída vai para um pipe.
- O texto de `help` do ASTER documenta os comandos e a composição novos.
- Estados 1.2 são migrados automaticamente: `/proc` é criado na primeira abertura.

### Corrigido

- Nenhuma regressão conhecida: as 5 suítes anteriores continuam passando.

## [1.2.0] — anterior

- Interface em inglês, auroraSYNC (login local, perfis, bloqueio de sessão),
  resoluções virtuais, repositório offline de pacotes, Lua 5.4.8 com Lua Studio
  e builds para Windows.

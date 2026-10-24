# Changelog

Todas as mudanças relevantes do AURORA/86. O formato segue
[Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/) e o projeto usa
versionamento semântico no formato `MAJOR.MINOR.PATCH`.

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
  (comando `screenshot`) e referência de teclado com `F1`.
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

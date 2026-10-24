# AURORA/86 — Ideias para as próximas versões

> **Status (1.3.0):** este documento nasceu como cardápio de ideias. A versão 1.3
> implementou os itens 2.2 (ASTER com pipes), 3.7 (`/proc`), 3.9 (A16ARC), 2.4
> (barra de janelas), 2.5 (protetor de tela), 5.2/5.4 do bloco rápido (captura de
> tela e referência de teclado) e, como base de tudo, o **item 2.1 (A16 BASIC)**.
> Os itens ainda abertos seguem abaixo, sem alterações, para a próxima versão.


Documento de trabalho: o que já existe, o que faz mais sentido implementar a seguir e por quê.
Não é uma promessa de roadmap — é um cardápio priorizado.

> **Nota de cronologia:** o projeto se apresenta como uma estação de **outubro de 1986** (veja
> `/etc/release`, o README e a data simulada). As ideias abaixo assumem 1986. Se a intenção for
> 1985, a solução mais elegante é tornar o ano um campo do SETUP da BIOS (item 9) em vez de
> reescrever textos soltos pelo código.

---

## 1. O que o AURORA/86 já é

| Área | Estado atual |
| --- | --- |
| Base | C++20 + Qt 6.8/WebEngine, estado em `vfs.json`, 4096 KiB RAM simulada, disco de 2048 KiB |
| Shell | ASTER com ~30 comandos, aspas, histórico, Tab; **sem pipes, sem glob, sem variáveis** |
| Apps | 20 janelas (`applications[]` em `src/desktop.cpp`): Terminal, Disk 0, Writer, Calc, Atelier, Control Panel, Monitor, Network Mail, Package Center, htop, Lua Studio, about |
| Jogos | 5 (Prism, Orbital Garden, Echo, Circuit, Orbital Watch) + Games Room |
| Pacotes | 16 pacotes offline / 23 utilitários simulados, com dependências e `man` |
| Extensibilidade | Lua 5.4 com `aurora.*` (janela, botões, read/write/list/mkdir, theme/setting), sandbox de 1 MiB |
| Sistema | Login auroraSYNC, perfis, bloqueio de sessão, temas, papel de parede, presets de vídeo, scanlines, áudio de speaker |
| Qualidade | 5 suítes de teste, `--self-test`, packaging AppImage + Windows |

A base é boa: o que falta não é "mais um app", é **profundidade de plataforma** — o que faz o
simulador parecer um computador de verdade e não um tema.

### Critério de priorização

Toda ideia foi avaliada por três perguntas:

1. **Autenticidade** — isso poderia existir num micro de 1986?
2. **Alavancagem** — usa infraestrutura que já existe (VFS, Speaker, processes, settings)?
3. **Demonstração** — gera screenshot/vídeo que faz alguém querer baixar?

Esforço: **S** = até 1 dia · **M** = 2–4 dias · **L** = 1–2 semanas.

---

## 2. Prioridade máxima — o que eu faria primeiro

### 2.1 A16 BASIC (interpretador dentro do terminal) — **L, maior impacto**

O que mais falta no AURORA/86 não é um app: é a linguagem que definia um micro de 1986.
Um computador pessoal dessa época **liga e cai no BASIC**. Hoje o AURORA cai num Unix-ish.

**Escopo mínimo viável**

```
ASTER> basic
A16 BASIC v1.0 — 4096 KiB free
OK
10 PRINT "AURORA/86"
20 FOR I = 1 TO 10
30 PRINT I; " SQUARED IS "; I*I
40 NEXT I
50 GOTO 70
70 END
RUN
```

- Modo linha numerada + modo direto (`PRINT 2+2` sem número de linha)
- Comandos: `RUN`, `LIST`, `NEW`, `SAVE "nome.bas"`, `LOAD "nome.bas"`, `CONT`, `TRON/TROFF`
- Statements: `PRINT` (`;` e `,`), `INPUT`, `LET`, `IF/THEN/ELSE`, `FOR/NEXT/STEP`, `GOTO`, `GOSUB/RETURN`,
  `DIM`, arrays 1D/2D, `DATA/READ/RESTORE`, `REM`, `CLS`, `RND`, `INT`, `ABS`, `SQR`, `SIN`, `COS`, `TAB`, `CHR$`, `ASC$`
- `BEEP` chamando `Speaker::play` — o clássico som de programa de 1986
- Erros com número de linha: `?SYNTAX ERROR IN 30`

**Por que é barato apesar do tamanho:** você já tem o padrão de sandbox em `src/lua_runtime.cpp`
(orçamento de instruções, limite de memória, limite de saída de 64 KiB). O BASIC é um
tokenizador + parser de linha + interpretador de statements, reaproveitando exatamente essas
traves. Sem alocação dinâmica além dos arrays, sem GC.

**Ganchos:** novo `src/basic.h/.cpp` em `aurora_core`; comando `basic` em `src/shell.cpp`;
`.bas` de exemplo no `seed()` de `src/system.cpp`; teste em `tests/basic_test.cpp`.

**Bônus que fecha o ciclo:** permitir `RUN` de um `.bas` no boot (um "AUTOEXEC.BAS" do
`/home/guest`) e um `aurora.basic()` na API Lua — mods passam a poder rodar listagens clássicas.

---

### 2.2 ASTER de verdade: pipes, redirecionamento, glob e variáveis — **M, melhor relação custo/benefício**

Hoje o parser (`tokenize` em `src/shell.cpp`) resolve uma linha simples. Adicionar uma camada de
composição transforma 23 utilitários em centenas de combinações — e é o que um usuário de 1986
espera de um shell inspirado em Unix.

- `|` pipe entre comandos (`ls | grep txt | wc -l`) — basta encadear `stdout` como argumento
- `>` e `>>` já existem; falta `2>` e `<` (stdin)
- `&&` e `;` para sequência (a lista `operators` já é coletada no tokenizador — provavelmente
  é meia hora de trabalho)
- Globbing `*`, `?`, `[abc]` resolvido contra a VFS
- Variáveis de ambiente: `$HOME`, `$USER`, `$PWD`, `$PATH`; comandos `export`, `env`, `set`
- Aliases persistentes em `/home/guest/.profile`, `alias ll="ls -l"`, `source .profile`
- `which`, `type`, `history` e `history | grep`
- Códigos de saída e `$?`

**Impacto colateral:** toda a documentação de pacotes (`man`) fica mais útil de graça.

---

### 2.3 Atelier vira editor de imagens ("Pixel Studio") — **M**

O Atelier hoje **visualiza** imagens e o `seed()` até gera um PPM procedural. Falta desenhar.

- Paleta fixa de 4 tons do adaptador LUME (é a limitação histórica e vira identidade visual)
- Lápis, linha, retângulo, elipse, balde, borracha, amostrador, zoom 8×, grade
- Undo/redo por snapshot (imagens pequenas, cabe no orçamento)
- Salvar/abrir `.ppm` na VFS; "Usar como papel de parede" (reaproveita a ponte de importação)
- "Exportar para o Linux..." usando a ponte explícita que já existe no wallpaper
- Sprites pequenos (16×16 / 32×32) para usar em mods Lua

**Por que:** é o app retrô que mais rende screenshot e o que mais combina com o resto (wallpaper,
Atelier, Lua). Reaproveita `createImageViewer` (`src/apps.cpp`) em vez de começar do zero.

---

### 2.4 Taskbar de verdade + ciclo de vida de janelas — **S/M**

As janelas minimizadas hoje só voltam pelo menu **Windows**. Uma barra inferior com as janelas
abertas (ícone + título), relógio, medidor de RAM e indicador de disco 0 é o "chrome" que falta
para parecer um sistema operacional.

- Barra inferior fixa: janelas abertas, clique para focar/restaurar, X para fechar
- `Alt+Tab` e `Alt+F4`
- Persistir layout ("reabrir as janelas da última sessão")
- Snap para grade de 8 px e lembrar posição/tamanho por app
- Confirmação ao fechar app com alterações não salvas (o `editorMayClose` já existe — generalize)

---

### 2.5 Screensaver + tela de descanso — **S**

Autenticidade pura e barata: depois de N minutos ocioso, entra um protetor de tela.

- Starfield, logo quicando (o clássico), "Lissajous" e amonites
- Configurável no Control Panel: tempo, tipo, senha ao acordar (usa o bloqueio auroraSYNC que já existe)
- Respeita `AURORA_NO_AUDIO` e `offscreen` nos testes

---

## 3. Profundidade de sistema — fazer o A16 parecer hardware

### 3.1 SETUP da BIOS (tela de POST antes do boot) — **S/M**
Contagem de memória, detecção de disco, "PRESS DEL TO ENTER SETUP", e um painel com data/hora
simulada, ano da estação (1985/1986), tamanho de RAM, ordem de boot, som de erro e "restore
defaults". O `beginBoot()` já é uma sequência de passos — é só dar a ela uma segunda tela e uma
tecla de escape.

### 3.2 RAM simulada que dói — **S/M**
`applications[]` já declara KiB por app, e `startProcess()` já registra memória. Falta **recusar**:
abrir o Network Mail (768 KiB) com 300 KiB livres deve mostrar "Cannot allocate 768 KiB — close a
window and try again". Transforma um número decorativo em mecânica de jogo. Bônus: `swap` simulado
em disco com penalidade (interrompido por som de drive), e o Monitor mostrando pressão de memória.

### 3.3 Impressora A16 — **S/M**
`lpr arquivo`, fila em `/var/spool/print`, janela de pré-visualização em papel contínuo 80 colunas,
`Ctrl+P` no Writer, `PRINT` do BASIC mandando para a impressora, e exportação para PNG/PDF.
Um micro de 1986 sem impressora é um micro incompleto.

### 3.4 Drive B: e disquetes — **M**
A VFS é um mapa plano de caminhos; um prefixo de volume resolve. `format b:`, `mount b:`,
`copy a: x.txt b:`, ejetar com aviso de escrita pendente, e um `Disk 1` no gerenciador de arquivos
com 360 KiB e ícone de disquete. Som de drive ao ler/gravar (`Speaker` já tem eventos).

### 3.5 Modem + BBS local — **M/L, a maior "história" depois do BASIC**
Um BBS simulado, offline, é o sonho de consumo de 1986:
`atdt 555-0142` → handshake a 1200 baud com barulho, ANSI art, mural de mensagens, e-mail entre
usuários locais, download de "arquivos" da VFS com velocidade fake (`1.2 KiB/s`) e contador de
minutos. Salva em `/var/bbs/` e integra com o Network Mail. Zero dependência de internet — só
simulação convincente. É o tipo de coisa que vira vídeo no YouTube.

### 3.6 Usuários reais e permissões — **M**
`/home/guest` é fixo no código. Com um mapa `usuários → uid`, `su`, `login`, `chmod` e `chown`
**aplicados de fato**, o modelo de permissões que já está em `VEntry` deixa de ser decorativo.
Dois usuários de fábrica (`guest` e `ada`) e um `/etc/passwd` navegável. `/bin`, `/sys`, `/etc`
seguem protegidos.

### 3.7 Fontes de dados vivos — **S**
`/proc` simulado com arquivos de verdade: `/proc/cpuinfo`, `/proc/meminfo`, `/proc/uptime`,
`/proc/<pid>/cmdline`. Assim `cat /proc/meminfo | grep MemFree` funciona sem inventar nada — e
combina com os pipes do item 2.2.

### 3.8 Scheduler e jobs — **M**
`at`/`cron` simulados: agendar um comando ou um script Lua para "daqui a 2 minutos". O job aparece
em `ps`/htop, consome memória simulada e grava saída em arquivo. Dá vida ao sistema sem interação.

### 3.9 Arquivador `.arc` — **S**
`pack`/`unpack` com zlib (`qCompress`/`qUncompress`, já disponível no Qt). Autenticidade de 1986 +
utilidade real para mover arquivos entre discos virtuais, e um `tar`/`zip` fake com listagem.

### 3.10 Tone Studio (tracker de 3 canais) — **M**
O `Speaker` já gera ondas. Uma grade estilo tracker (3 canais de tom + canal de ruído), arquivo
`.snd` na VFS, play/loop, e exportar WAV para o disco virtual. Trilha sonora para os jogos e um
app que ninguém espera num simulador.

---

## 4. Conteúdo e jogos

| Ideia | Por quê | Esforço |
| --- | --- | --- |
| **Aventura em texto** ("The Aurora Incident") com parser em português e inglês | Gênero dominante em 1986, roda no terminal, casa com o Lua | M |
| **Klondike / Reversi / Sokoban** | Faltam os "clássicos de pacote"; Sokoban é 1982, Reversi é 1970s | M cada |
| **Tabela de recordes unificada** (`/home/guest/.scores.json`) + tela "Hall of Fame" na Games Room | Hoje cada jogo guarda o seu recorde; unificar cria meta-coletiva | S |
| **Desafio diário** com seed fixa nos puzzles | Motivo para reabrir o app amanhã; usa `clockOffsetSeconds` | S |
| **Demo da Aurora** (plasma, estrelas, scroller em 4 tons) | O "demo scene" era o cartão de visita do hardware — e é screenshot instantâneo | S/M |
| **Editor de fases para Prism/Garden** salvo na VFS | Conteúdo gerado pelo usuário, reaproveita o formato dos puzzles | M |

---

## 5. Experiência, onboarding e retenção

### 5.1 Tour de boas-vindas — **S**
Hoje a primeira execução é: boot → login → desktop vazio. Um tour curto (destacar ícone,
"digite `help`", "instale o fastfetch") reduz abandono. O `README.txt` já faz isso em texto — falta
a camada visual.

### 5.2 Conquistas / scrapbook — **S/M**
Selos guardados na VFS: primeiro pacote instalado, primeiro mod Lua rodando, Prism completo,
100 comandos digitados, primeiro BBS. Uma aba "Scrapbook" no About. Custo baixo, retenção alta.

### 5.3 Efeitos de CRT upgrade — **S**
Além das scanlines: curvatura leve, persistência de fósforo, flicker, fósforo verde/âmbar/branco,
modo "VHS". Configuração por tema. É o que muda a primeira impressão de um screenshot.

### 5.4 Temas como arquivos na VFS — **S/M**
Um `.theme` em texto (chaves = valores), editável no Writer, com `theme import/export`. Transforma
tema em conteúdo que o usuário compartilha — e barateia a criação de temas oficiais.

### 5.5 Internacionalização PT-BR / EN — **M**
O projeto é de autor brasileiro com README bilíngue, mas a UI é só inglesa. Um `QTranslator` (ou
uma tabela `ui-strings.json` na VFS) com seletor no Control Panel aumenta muito o alcance. Atenção:
testes de UI (`tests/ui_test.cpp`) verificam textos em inglês — precisa de um modo estável.

### 5.6 Captura de tela integrada — **S**
`Desktop::screenshot()` já existe. Amarre em `Print Screen`: salva em `/home/guest/images/`,
abre no Atelier e oferece "exportar para o Linux" (a ponte explícita já existe). Ótimo para
documentar e divulgar.

### 5.7 Cheat sheet de teclado (Shift+F1) — **S**
Overlay com atalhos, comandos do ASTER e dicas do Lua Studio. Barato e melhora a descoberta.

---

## 6. Plataforma: mods e arquitetura

### 6.1 API Lua mais expressiva — **M**
O que falta para a comunidade criar apps de verdade:

- `aurora.dialog { title, fields = {...} }` — formulário com campos tipados
- `aurora.menu { ... }` e `aurora.status("texto")` na barra inferior
- `aurora.fs` com rename/copy/remove e leitura de tamanho
- `aurora.store` — armazenamento próprio por mod (sem brigar por arquivos soltos)
- `aurora.basic("10 PRINT ...")` — ponte para o interpretador
- `aurora.net.get(url)` servido por um **cache offline** de páginas de exemplo (mantém a promessa
  de "sem rede real" e abre a possibilidade de BBS em Lua)
- Manifesto `-- @permissions: fs,ui` com prompt de consentimento na instalação
- `-- @screenshot` no README do mod: a "loja" mostra preview

### 6.2 Registro de aplicativos (fim do array hardcoded) — **M**
Hoje `applications[]` em `src/desktop.cpp` mistura id, título, memória e tamanho — e o menu
`Applications` filtra uma lista fixa de ids. Um registro (`AppDescriptor` + `registerApp()`) faria
apps nativos e mods Lua aparecerem do mesmo jeito, com ícone, memória e categoria declarados.

### 6.3 Tela de recuperação — **S**
Se `vfs.json` estiver corrompido, em vez de só "estado inválido preservado", mostrar uma tela
"STATION RECOVERY" com: tentar de novo, abrir sem estado, abrir a pasta do estado, e um backup
automático rotativo (`vfs.json.1`…`.3`). Segurança percebida e churn baixo.

### 6.4 Estado: compressão e escrita incremental — **S/M**
O `vfs.json` é reescrito inteiro e guarda todos os bytes em base64. Para discos cheios e muitos
arquivos, comprimir o estado (`qCompress`, com formato/flag para manter compat) e marcar
dirty-only reduz I/O. Manter leitura de estados 1.1/1.2 — já é um compromisso do README.

### 6.5 Qualidade — **S/M**
- Teste de fumaça para **cada** app do registro (abrir, interagir, fechar) — hoje só alguns têm
- Testes golden de renderização (hash do frame) para pegar regressão visual
- Fuzz do `tokenize` do shell e do leitor de banco de pacotes
- `--self-test --report json` para CI

---

## 7. Distribuição e comunidade

- **CHANGELOG.md** + "What's new" no About (hoje a novidade só existe no README)
- **Galeria no site/README**: screenshot por app, não só 4
- **`--state-dir` com estações de exemplo** ("estação com BBS cheio", "estação recém-instalada")
  para reviews e testes — já suportado pela CLI
- **Exportar/importar estação** (um `.a86station` compactado) para o usuário levar o disco virtual
  entre máquinas; convergência com o item 6.4
- **Release notes em PT-BR e EN**, e um `SECURITY.md` explicando as fronteiras (nada toca o host
  sem ponte explícita) — isso é um diferencial real do projeto e merece ser dito

---

## 8. Sequência sugerida

| Versão | Tema | Conteúdo |
| --- | --- | --- |
| **1.3 — "Programmable"** | Fazer o computador programável | **BASIC** · pipes/glob/variáveis no ASTER · `/proc` simulado · cheat sheet Shift+F1 |
| **1.4 — "Studio"** | Criar, não só usar | **Pixel Studio** · Tone Studio · temas como arquivo · captura de tela · `.arc` |
| **1.5 — "Workstation"** | Parecer um SO completo | **Taskbar** · SETUP da BIOS · RAM que recusa · impressora · Drive B: |
| **1.6 — "Online"** | O sonho de 1986 | **Modem + BBS** · usuários e permissões · scheduler · conquistas |
| **1.7 — "Worlds"** | Conteúdo | Aventura em texto · 2–3 clássicos · editor de fases · desafio diário |

Cada release com: 1 feature grande de plataforma, 2 médias, 3–5 pequenas de polimento. O padrão
da 1.2 (auroraSYNC + pacotes + Lua + Windows) já mostra que esse tamanho cabe.

---

## 9. Ganhos rápidos (cabe em um fim de semana)

1. `screensaver` com logo quicando e senha ao acordar
2. `Ctrl+Alt+Del` abrindo uma tela "System" com `ps` resumido (e brincadeira no rodapé)
3. `Print Screen` → `/home/guest/images/snap-NN.png`
4. Relógio/calendário/despertador como desk accessory
5. Calculadora científica e programadora (hex/oct/bin, 16 bits) na mesma janela
6. `fortune` na abertura do terminal, uma vez por dia (já existe o pacote)
7. `beep` no shell e no BASIC (o `Speaker` já aceita eventos)
8. `man aurora` + `aurora --help` revisados para refletir todos os comandos novos
9. Contador de uptime e "login since" no fastfetch
10. Easter eggs em `/etc/motd` e nos créditos do About

---

## 10. O que eu **não** faria agora

- **Emulação real de CPU / rodar binários do Linux** — muda a natureza do projeto, destrói a
  sandbox e não é o que faz o AURORA ser charmoso.
- **Rede real para os mods Lua** — a promessa de "nada toca o host sem ponte explícita" é um
  diferencial; o BBS simulado entrega a fantasia com risco zero.
- **Mais três jogos de ação** — já há cinco; o retorno agora está em plataforma (BASIC, shell,
  ferramentas), não em mais um arcade.
- **UI moderna (flat, animações longas, dark mode genérico)** — quebra a identidade. O caminho é
  aprofundar o visual 1986 (CRT, fósforo, tipografia mono), não diluí-lo.
- **Reescrever a VFS em SQLite** — o `vfs.json` atômico é simples, testável e suficiente; otimize
  (item 6.4) antes de trocar.

---

## 11. Como saber se a próxima versão funcionou

- O usuário consegue **escrever um programa** (BASIC ou Lua) em menos de 3 minutos após o primeiro boot
- Existe pelo menos **um screenshot novo** que ninguém confunde com um app moderno
- `ctest` cobre todas as features novas e o `--self-test` abre todos os apps
- O README encurta: o usuário descobre as coisas na interface, não na documentação

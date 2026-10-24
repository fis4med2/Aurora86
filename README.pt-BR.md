# AURORA/86

[English](README.md) · [Baixar v1.1.0](https://github.com/fis4med2/Aurora86/releases/tag/v1.1.0)

Computador pessoal fictício de 1986, escrito em **C++20 e Qt 6 para Linux**. Boot próprio, desktop, terminal inspirado no Unix, disco virtual compartilhado e aplicativos funcionais. Interface e documentos do sistema em português brasileiro.

![Desktop AURORA/86](AURORA86-desktop.png)

## Novidades da versão 1.1

- **Seu papel de parede:** importe uma imagem do Linux, veja a prévia e escolha preencher, ajustar, esticar ou centralizar. Filtro retrô de quatro tons opcional. O simulador guarda uma cópia reduzida.
- **Letras legíveis:** DejaVu Sans Mono incorporada, suavização de texto e tamanhos de 14, 16, 18 ou 20 px, salvos entre sessões. Padrão: 16 px. Controles e janelas maiores.
- **Três jogos novos:** Prisma, Jardim Orbital e Eco, além de Circuito e Vigília Orbital. Cinco jogos funcionais na nova Sala de jogos.

![Controle do papel de parede](AURORA86-papel-de-parede.png)
![Sala de jogos](AURORA86-jogos.png)
![Quebra-cabeça Prisma](AURORA86-prisma.png)

## Executar

Baixe o executável em [Releases](https://github.com/fis4med2/Aurora86/releases). O AppImage é **x86_64 e exige glibc 2.44 ou superior**, sessão X11/Wayland e drivers gráficos do Linux. Qt, Chromium/WebEngine, fontes e bibliotecas de áudio estão incluídos. Distribuições antigas precisam de outra compilação sobre uma base mais antiga.

```sh
chmod +x AURORA86-x86_64.AppImage
./AURORA86-x86_64.AppImage
```

Sem FUSE:

```sh
./AURORA86-x86_64.AppImage --appimage-extract-and-run
```

**F11** alterna tela cheia. Enter/Espaço pula o boot. Arraste a barra para mover janelas; redimensione pelo canto inferior direito. O menu Janelas restaura aplicativos minimizados.

Para usar sua imagem: **Controle → Papel de parede → Importar imagem do Linux**. Também é possível clicar com o botão direito no desktop. Para aumentar as letras: **Controle → Sistema e letras**. A importação não altera a imagem original.

## Aplicativos

- **Terminal ASTER:** `help`, `ls -l`, `cd`, `pwd`, `cat`, `echo` com `>`/`>>`, `mkdir`, `touch`, `rm -r`, `mv`, `cp`, `chmod`, `stat`, `df`, `free`, `ps`, `kill`, `date`, `uname`, `whoami`, `clear`. Aspas, histórico e Tab para completar caminhos. Jogos por `prisma`, `jardim`, `eco`, `snake`, `blocks` ou `games`. `exit` fecha o terminal; `shutdown` encerra o simulador.
- **Disco 0:** criação, abertura, exclusão e movimentação de arquivos e pastas virtuais.
- **Escritor:** edição UTF-8, Ctrl+S e confirmação de alterações não gravadas ou substituição.
- **Calculadora, visualizador de imagens, monitor e configurações.**
- **Correio de Rede:** HTTP/HTTPS reais pelo Chromium, navegação e gravação de HTML no disco virtual. Acesso a arquivos locais, uploads, downloads para o Linux e permissões de dispositivos ficam desativados. A proteção do Chromium permanece ativa.

## Jogos

| Jogo | Como jogar |
| --- | --- |
| Prisma | Gire espelhos para levar o laser ao receptor. Seis níveis; estado do puzzle salvo. |
| Jardim Orbital | Planeje movimentos de irrigação em cruz para restaurar o jardim. Oito puzzles; progresso salvo. |
| Eco | Observe, escute e repita a sequência nos botões com mouse ou teclado. Recorde persistente. |
| Circuito | Setas/WASD roteiam um sinal crescente sem cruzar o próprio caminho. Espaço pausa; Enter reinicia. |
| Vigília Orbital | Setas/WASD movem e Espaço atira. P pausa; Enter reinicia. Três escudos e recorde salvo. |

## Persistência e limites

O A16 simula 8 MHz, **4096 KiB de RAM** e **2048 KiB de disco**. Arquivos, permissões, configurações, papel de parede e progresso/recordes são gravados atomicamente em `vfs.json`, em `~/.local/share/Aurora/AURORA86/` ou no diretório XDG correspondente. `--state-dir CAMINHO` seleciona outra estação. Estados da versão 1.0 continuam utilizáveis.

`/bin`, `/sys` e `/etc` são protegidos; `/home/guest` e `/tmp` aceitam gravação. Caminhos virtuais não acessam arquivos Linux e o terminal não executa comandos do host. A seleção de papel de parede é uma ponte explícita de importação, somente leitura. O relógio não altera o Linux. Estado inválido é preservado; uma trava impede sobrescritas por sessões simultâneas.

É um simulador de ambiente, não um emulador de instruções da CPU nem um Unix completo. As quotas de RAM são simuladas; o Chromium consome memória real conforme as páginas. Sessões do navegador ficam em memória, DRM não está incluído e alguns sites dependem de capacidades desativadas. Partidas de ação reiniciam; puzzles e recordes persistem.

## Compilar e testar

Requer CMake ≥3.21, compilador C++20 e Qt ≥6.8 com Core, Widgets, Multimedia, WebEngineWidgets e Test.

```sh
cmake -S . -B work/build -DCMAKE_BUILD_TYPE=Release
cmake --build work/build -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir work/build --output-on-failure
./work/build/aurora86
bash packaging/build-appimage.sh
```

O empacotador usa Python 3, curl, ldd e readelf. Ferramentas oficiais AppImage são baixadas com SHA-256 fixado. glibc e drivers gráficos pertencem ao host; o requisito calculado fica em `COMPATIBILIDADE.txt`. Para ampliar compatibilidade, reconstrua também as dependências numa distribuição mais antiga.

`--self-test` verifica os aplicativos sem rede. `--browser-check https://example.com` verifica navegação real. `AURORA_NO_AUDIO=1` desativa áudio; testes offscreen fazem isso automaticamente. Imagens importadas são reduzidas até 960×640 e respeitam a capacidade do disco virtual.

Fontes em `src/`, testes em `tests/`, metadados e empacotamento em `packaging/`. Executável e capturas também ficam diretamente na pasta principal. O AppImage é disponibilizado nos assets da release, pois ultrapassa o limite normal de arquivos Git do GitHub.

## Distribuição e licença

O GitHub hospeda fontes, capturas e releases AppImage. **Não houve submissão ao Flathub.** A [política atual](https://docs.flathub.org/docs/for-app-authors/requirements#generative-ai-policy) proíbe manifestos de submissão gerados/assistidos por IA e interações de submissão feitas por agentes. Um mantenedor humano precisa conduzir uma eventual submissão conforme essas regras e revisar os demais requisitos, incluindo interface em inglês e histórico de desenvolvimento.

A maior parte do código original, documentação e empacotamento foi gerada ou editada com assistência de IA, depois compilada e testada. Bibliotecas e fontes de terceiros mantêm suas licenças. Código do projeto: **GPL-3.0-or-later**. Licenças de fontes em `assets/`; avisos das dependências em `usr/share/licenses` dentro do AppImage extraído.

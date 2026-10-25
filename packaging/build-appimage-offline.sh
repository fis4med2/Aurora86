#!/usr/bin/env bash
# Monta o AppImage da AURORA/86 sem baixar nada da rede.
#
# Use este script quando o runtime type-2 e o mksquashfs já estão no disco (por
# exemplo em uma máquina sem acesso às releases do GitHub). O layout do AppDir é
# o mesmo de packaging/build-appimage.sh; a diferença é que as dependências são
# resolvidas por readelf em vez de ldd, para que o empacotamento funcione em um
# host que não tem todas as bibliotecas de sistema instaladas.
#
# Uso:
#   packaging/build-appimage-offline.sh <binário> <saída.AppImage>
#
# Variáveis:
#   QT_PREFIX         prefixo do Qt (padrão: /usr) com lib/, plugins/, libexec/,
#                     resources/ e translations/qtwebengine_locales/
#   AURORA_RUNTIME    runtime type-2 do AppImage (obrigatório)
#   AURORA_MKSQUASHFS mksquashfs (obrigatório)
#   MKSQUASHFS_COMP   compressor (padrão: gzip; use zstd se o runtime suportar)
#   MKSQUASHFS_LEVEL  nível de compressão (ex.: 22 para zstd)
#   MKSQUASHFS_BLOCK  tamanho de bloco (ex.: 1M)
set -euo pipefail

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary=${1:?"informe o binário compilado"}
output=${2:-"$project_root/outputs/AURORA86-x86_64.AppImage"}
qt_prefix=${QT_PREFIX:-/usr}
runtime=${AURORA_RUNTIME:?"defina AURORA_RUNTIME com o caminho do runtime type-2"}
mksquashfs=${AURORA_MKSQUASHFS:?"defina AURORA_MKSQUASHFS com o caminho do mksquashfs"}
comp=${MKSQUASHFS_COMP:-gzip}

[[ $(uname -m) == x86_64 ]] || { echo 'Este pacote requer host x86_64.' >&2; exit 1; }
[[ -x "$binary" ]] || { echo "Binário inexistente: $binary" >&2; exit 1; }
[[ -x "$runtime" ]] || { echo "Runtime inexistente: $runtime" >&2; exit 1; }
[[ -x "$mksquashfs" ]] || { echo "mksquashfs inexistente: $mksquashfs" >&2; exit 1; }

appdir="$project_root/work/AURORA86.AppDir"
stage="$appdir"
rm -rf "$stage"
mkdir -p "$(dirname -- "$output")"
export AURORA_PACKAGE_ROOT="$project_root" AURORA_PACKAGE_BINARY="$binary" \
       AURORA_PACKAGE_APPDIR="$stage" AURORA_QT_PREFIX="$qt_prefix"

python3 - <<'PY'
import os, pathlib, re, shutil, subprocess

root = pathlib.Path(os.environ['AURORA_PACKAGE_ROOT'])
binary = pathlib.Path(os.environ['AURORA_PACKAGE_BINARY']).resolve()
stage = pathlib.Path(os.environ['AURORA_PACKAGE_APPDIR'])
qt = pathlib.Path(os.environ['AURORA_QT_PREFIX'])

for directory in ('usr/bin', 'usr/lib', 'usr/libexec', 'usr/plugins',
                  'usr/share/aurora86/fonts', 'usr/share/applications', 'usr/share/metainfo',
                  'usr/share/icons/hicolor/scalable/apps', 'usr/share/qt6/resources',
                  'usr/share/qt6/translations/qtwebengine_locales', 'usr/share/licenses'):
    (stage / directory).mkdir(parents=True, exist_ok=True)

shutil.copy2(binary, stage / 'usr/bin/aurora86')
doom_helper = binary.with_name('aurora86-doom')
if not doom_helper.is_file():
    raise RuntimeError(f'Compile também o motor Doom: {doom_helper}')
shutil.copy2(doom_helper, stage / 'usr/bin/aurora86-doom')
shutil.copy2(root / 'packaging/AppRun', stage / 'AppRun')
(stage / 'AppRun').chmod(0o755)
for target in ('aurora86.desktop', 'usr/share/applications/aurora86.desktop'):
    shutil.copy2(root / 'packaging/aurora86.desktop', stage / target)
for target in ('aurora86.svg', '.DirIcon', 'usr/share/icons/hicolor/scalable/apps/aurora86.svg',
               'usr/share/icons/hicolor/scalable/apps/io.github.fis4med2.Aurora86.svg'):
    shutil.copy2(root / 'packaging/aurora86.svg', stage / target)
for name, directory in (('io.github.fis4med2.Aurora86.desktop', 'applications'),
                        ('io.github.fis4med2.Aurora86.metainfo.xml', 'metainfo')):
    shutil.copy2(root / 'packaging' / name, stage / 'usr/share' / directory / name)
(stage / 'usr/bin/qt.conf').write_text(
    '[Paths]\nPrefix=..\nPlugins=plugins\nLibraryExecutables=libexec\n'
    'Data=share/qt6\nTranslations=share/qt6/translations\n')

process = qt / 'libexec/QtWebEngineProcess'
if not process.is_file():
    raise SystemExit(f'QtWebEngineProcess ausente: {process}')
shutil.copy2(process, stage / 'usr/libexec/QtWebEngineProcess')

seeds = [binary, process, doom_helper]
plugin_root = qt / 'plugins'
plugin_groups = ('platforms', 'xcbglintegrations', 'wayland-shell-integration',
                 'wayland-decoration-client', 'wayland-graphics-integration-client',
                 'platforminputcontexts', 'imageformats')
for group in plugin_groups:
    for source in sorted((plugin_root / group).glob('*.so')):
        if group == 'platforms' and source.name not in (
                'libqxcb.so', 'libqwayland-generic.so', 'libqwayland-egl.so',
                'libqoffscreen.so', 'libqminimal.so'):
            continue
        if group == 'imageformats' and source.name in ('libqpdf.so',):
            continue
        target = stage / 'usr/plugins' / group / source.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        seeds.append(source)

resource_root = qt / 'resources'
for source in sorted(resource_root.glob('*')):
    # devtools não fazem parte do produto e custam ~9 MB no pacote.
    if source.name == 'qtwebengine_devtools_resources.pak':
        continue
    if source.suffix in ('.pak', '.bin', '.dat'):
        shutil.copy2(source, stage / 'usr/share/qt6/resources' / source.name)
for locale in ('en-US', 'pt-BR'):
    source = qt / 'translations/qtwebengine_locales' / (locale + '.pak')
    if source.is_file():
        shutil.copy2(source, stage / 'usr/share/qt6/translations/qtwebengine_locales' / source.name)

for name in ('DejaVuSans.ttf', 'DejaVuSansMono.ttf'):
    matches = list(pathlib.Path('/usr/share/fonts').rglob(name))
    if matches:
        shutil.copy2(matches[0], stage / 'usr/share/aurora86/fonts' / name)

# glibc, driver ABI e bibliotecas do sistema continuam vindo do host: empacotar
# as do host de build quebraria máquinas compatíveis (diferentes versões de SO).
host_libraries = re.compile(
    r'^(ld-linux.*|lib(c|m|mvec|dl|pthread|rt|util|resolv|anl|BrokenLocale|nss_.*)\.so\..*'
    r'|lib(GL|GLX|GLdispatch|EGL|GLESv[12]|OpenGL|drm|drm_.*|gbm)\.so\..*'
    r'|lib(X11|Xext|Xrender|Xrandr|Xi|Xfixes|Xcursor|Xcomposite|Xdamage|Xtst|xcb.*|xkbcommon.*'
    r'|wayland.*|dbus-1|nss3|nssutil3|smime3|ssl3|nspr4|plc4|plds4|softokn3|freebl3|nssckbi'
    r'|asound|pulse|pipewire|avcodec|avformat|avutil|swresample|swscale|fontconfig|freetype'
    r'|expat|z|zstd|lzma|bz2|png16|jpeg|harfbuzz|glib-2\.0|gobject-2\.0|gio-2\.0|gmodule-2\.0'
    r'|gthread-2\.0|udev|systemd|selinux|mount|blkid|uuid|crypto|ssl|stdc\+\+|gcc_s)\.so\..*)$')

def needed(path):
    data = subprocess.run(['readelf', '-d', str(path)], capture_output=True, text=True).stdout
    return re.findall(r'\(NEEDED\)\s+Shared library: \[([^\]]+)\]', data)

def find(soname):
    for directory in (qt / 'lib', qt / 'lib/x86_64-linux-gnu'):
        candidate = directory / soname
        if candidate.exists():
            return candidate.resolve()
    return None

queue = list(seeds)
seen, dependencies, host_dependencies = set(), {}, set()
while queue:
    source = pathlib.Path(queue.pop()).resolve()
    if source in seen:
        continue
    seen.add(source)
    for soname in needed(source):
        if host_libraries.match(soname):
            host_dependencies.add(soname)
            continue
        if soname in dependencies:
            continue
        located = find(soname)
        if located is None:
            host_dependencies.add(soname)
            continue
        dependencies[soname] = located
        queue.append(located)

for soname, source in sorted(dependencies.items()):
    shutil.copy2(source, stage / 'usr/lib' / soname)

version = (0, 0)
for source in [stage / 'usr/bin/aurora86', stage / 'usr/libexec/QtWebEngineProcess',
               *list((stage / 'usr/lib').glob('*')), *list((stage / 'usr/plugins').rglob('*.so'))]:
    data = subprocess.run(['readelf', '--version-info', str(source)], capture_output=True, text=True).stdout
    for major, minor in re.findall(r'GLIBC_(\d+)\.(\d+)\b', data):
        version = max(version, (int(major), int(minor)))

metadata = ('AURORA/86 Linux x86_64\n'
            f'glibc mínima exigida pelas bibliotecas empacotadas: {version[0]}.{version[1]}\n'
            'Requer kernel Linux, glibc, sessão gráfica X11/Wayland e drivers gráficos do host.\n'
            'Qt, WebEngine/Chromium, recursos web, fontes e ícones incluídos.\n'
            'Lua 5.4 incorporado; programas e mods não exigem instalação separada.\n'
            'Bibliotecas fornecidas pelo host: ' + ', '.join(sorted(host_dependencies)) + '\n')
(stage / 'COMPATIBILITY.txt').write_text(metadata)
(root / 'outputs').mkdir(exist_ok=True)
(root / 'outputs/COMPATIBILIDADE.txt').write_text(
    metadata + '\nExecutar:\n  chmod +x AURORA86-x86_64.AppImage\n  ./AURORA86-x86_64.AppImage\n\n'
    'Sem FUSE:\n  ./AURORA86-x86_64.AppImage --appimage-extract-and-run\n\n'
    'Extrair:\n  ./AURORA86-x86_64.AppImage --appimage-extract\n  ./squashfs-root/AppRun\n')

for name in ('LICENSE', 'README.md', 'README.pt-BR.md'):
    if (root / name).is_file():
        shutil.copy2(root / name, stage / name)
shutil.copytree(root / 'mods', stage / 'mods', dirs_exist_ok=True)
(licence_root := stage / 'usr/share/licenses').mkdir(parents=True, exist_ok=True)
(licence_root / 'lua').mkdir(exist_ok=True)
shutil.copytree(root / 'third_party/doom', licence_root / 'doom',
                ignore=shutil.ignore_patterns('*.wad', '*.h'))
for name in ('LICENSE.txt', 'UPSTREAM.txt'):
    shutil.copy2(root / 'third_party/lua' / name, licence_root / 'lua' / name)
for source in (root / 'assets').glob('*LICENSE*'):
    shutil.copy2(source, licence_root / source.name)
(licence_root / 'BUNDLED-LIBRARIES.txt').write_text(
    '\n'.join(f'{soname} <- {source}' for soname, source in sorted(dependencies.items())) + '\n')
print(f'AppDir pronto: {len(dependencies)} bibliotecas Qt/ICU empacotadas; '
      f'glibc mínima {version[0]}.{version[1]}; {len(host_dependencies)} dependências do host')
PY

rm -f "$output"
mksquashfs_extra=()
if [[ -n ${MKSQUASHFS_LEVEL:-} ]]; then mksquashfs_extra+=(-Xcompression-level "$MKSQUASHFS_LEVEL"); fi
if [[ -n ${MKSQUASHFS_BLOCK:-} ]]; then mksquashfs_extra+=(-b "$MKSQUASHFS_BLOCK"); fi
"$mksquashfs" "$stage" "$output.squashfs" -comp "$comp" "${mksquashfs_extra[@]}" \
    -noappend -all-root -no-xattrs -no-progress 2>&1 \
    | grep -Ev '^(Parallel|Number of|Exportable|Filesystem|Fragment|Compressed|Uncompressed|Percentage|Files|File size|Directories|Reading|Writing|Creating)' || true
cat "$runtime" "$output.squashfs" > "$output"
rm -f "$output.squashfs"
chmod +x "$output"
(cd "$(dirname -- "$output")" && sha256sum "$(basename -- "$output")") > "$output.sha256"
du -h "$output"
printf 'AppImage criado: %s\n' "$output"

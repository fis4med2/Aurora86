#!/usr/bin/env bash
# Run on the oldest Linux baseline you intend to support. glibc and graphics
# drivers remain host components; all application/Qt libraries are bundled.
set -euo pipefail
project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary=${1:-"$project_root/work/build/aurora86"}
output=${2:-"$project_root/outputs/AURORA86-x86_64.AppImage"}
tools="$project_root/work/tools"
appdir="$project_root/work/AURORA86.AppDir"
[[ $(uname -m) == x86_64 ]] || { echo 'Este pacote requer host x86_64.' >&2; exit 1; }
[[ -x "$binary" ]] || { echo "Compile primeiro: $binary" >&2; exit 1; }
mkdir -p "$tools" "$(dirname -- "$output")"
fetch() {
    local url=$1 path=$2 expected=$3
    if [[ ! -f "$path" ]]; then curl -fL --retry 2 "$url" -o "$path"; fi
    printf '%s  %s\n' "$expected" "$path" | sha256sum -c -
    chmod +x "$path"
}
fetch 'https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage' \
    "$tools/appimagetool.AppImage" ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
fetch 'https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64' \
    "$tools/runtime-x86_64" 2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
if [[ ! -x "$tools/squashfs-root/AppRun" ]]; then
    (cd "$tools" && ./appimagetool.AppImage --appimage-extract >/dev/null)
fi
export AURORA_PACKAGE_ROOT="$project_root" AURORA_PACKAGE_BINARY="$binary" AURORA_PACKAGE_APPDIR="$appdir"
python3 - <<'PY'
import os, pathlib, re, shutil, subprocess

root = pathlib.Path(os.environ['AURORA_PACKAGE_ROOT'])
binary = pathlib.Path(os.environ['AURORA_PACKAGE_BINARY']).resolve()
stage = pathlib.Path(os.environ['AURORA_PACKAGE_APPDIR'])
if stage.exists():
    shutil.rmtree(stage)
for directory in ('usr/bin', 'usr/lib', 'usr/libexec', 'usr/plugins', 'usr/share/aurora86/fonts',
                  'usr/share/applications', 'usr/share/metainfo', 'usr/share/icons/hicolor/scalable/apps',
                  'usr/share/qt6/resources', 'usr/share/qt6/translations/qtwebengine_locales',
                  'usr/share/licenses'):
    (stage / directory).mkdir(parents=True, exist_ok=True)
qt = {key: subprocess.check_output(['qmake6', '-query', key], text=True).strip()
      for key in ('QT_INSTALL_LIBS', 'QT_INSTALL_LIBEXECS', 'QT_INSTALL_DATA',
                  'QT_INSTALL_PLUGINS', 'QT_INSTALL_TRANSLATIONS')}
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
(stage / 'usr/bin/qt.conf').write_text('[Paths]\nPrefix=..\nPlugins=plugins\nLibraryExecutables=libexec\nData=share/qt6\nTranslations=share/qt6/translations\n')
process = pathlib.Path(qt['QT_INSTALL_LIBEXECS']) / 'QtWebEngineProcess'
if not process.is_file():
    raise RuntimeError(f'QtWebEngineProcess ausente: {process}')
shutil.copy2(process, stage / 'usr/libexec/QtWebEngineProcess')
seeds = [binary, process, doom_helper]
plugin_root = pathlib.Path(qt['QT_INSTALL_PLUGINS'])
plugin_groups = ('platforms', 'xcbglintegrations', 'wayland-shell-integration',
                 'wayland-decoration-client', 'wayland-graphics-integration-client', 'tls', 'multimedia')
for group in plugin_groups:
    candidates = list((plugin_root / group).glob('*.so'))
    if group == 'platforms':
        candidates = [p for p in candidates if p.name in ('libqxcb.so', 'libqwayland.so', 'libqoffscreen.so', 'libqminimal.so')]
    for source in candidates:
        target = stage / 'usr/plugins' / group / source.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        seeds.append(source)
for source in (plugin_root / 'imageformats').glob('libq*.so'):
    if source.name == 'libqpdf.so':
        continue
    target = stage / 'usr/plugins/imageformats' / source.name
    target.parent.mkdir(exist_ok=True)
    shutil.copy2(source, target)
    seeds.append(source)
compose = plugin_root / 'platforminputcontexts/libcomposeplatforminputcontextplugin.so'
if compose.is_file():
    (stage / 'usr/plugins/platforminputcontexts').mkdir(exist_ok=True)
    shutil.copy2(compose, stage / 'usr/plugins/platforminputcontexts' / compose.name)
    seeds.append(compose)
for name in ('libssl.so.3', 'libcrypto.so.3', 'libsoftokn3.so', 'libfreebl3.so', 'libnssckbi.so'):
    source = pathlib.Path(qt['QT_INSTALL_LIBS']) / name
    if source.exists():
        seeds.append(source)
resource_root = pathlib.Path(qt['QT_INSTALL_DATA']) / 'resources'
for source in resource_root.glob('*'):
    if source.suffix in ('.pak', '.bin', '.dat'):
        shutil.copy2(source, stage / 'usr/share/qt6/resources' / source.name)
for locale in ('en-US', 'pt-BR'):
    source = pathlib.Path(qt['QT_INSTALL_TRANSLATIONS']) / 'qtwebengine_locales' / (locale + '.pak')
    if not source.is_file():
        raise RuntimeError(f'Locale ausente: {source}')
    shutil.copy2(source, stage / 'usr/share/qt6/translations/qtwebengine_locales' / source.name)
for name in ('DejaVuSans.ttf', 'DejaVuSansMono.ttf'):
    matches = list(pathlib.Path('/usr/share/fonts').rglob(name))
    if matches:
        shutil.copy2(matches[0], stage / 'usr/share/aurora86/fonts' / name)
# Keep host glibc and GPU dispatch/driver ABI together. Bundling driver libraries
# from the build machine would break otherwise compatible Intel/AMD/NVIDIA hosts.
host_libraries = re.compile(r'^(ld-linux.*|lib(c|m|mvec|dl|pthread|rt|util|resolv|anl|BrokenLocale|nss_.*)\.so\..*|lib(GL|GLX|GLdispatch|EGL|GLESv[12]|OpenGL|drm|drm_.*|gbm)\.so\..*)$')
queue = list(seeds)
seen = set()
dependencies = {}
host_dependencies = set()
while queue:
    source = pathlib.Path(queue.pop())
    real = source.resolve()
    if real in seen:
        continue
    seen.add(real)
    result = subprocess.run(['ldd', str(real)], capture_output=True, text=True)
    for line in result.stdout.splitlines():
        if 'not found' in line:
            raise RuntimeError(f'Dependência não encontrada em {source}: {line.strip()}')
        match = re.search(r'^\s*(\S+)\s+=>\s+(/\S+)', line)
        if not match:
            continue
        soname, path = match.groups()
        soname = pathlib.Path(soname).name
        if host_libraries.match(soname):
            host_dependencies.add(soname)
            continue
        dependency = pathlib.Path(path)
        dependencies[soname] = dependency
        queue.append(dependency)
for seed in seeds[2:]:
    if seed.parent == pathlib.Path(qt['QT_INSTALL_LIBS']):
        dependencies[seed.name] = seed
for soname, source in sorted(dependencies.items()):
    shutil.copy2(source.resolve(), stage / 'usr/lib' / soname)
version = (0, 0)
for source in [stage / 'usr/bin/aurora86', stage / 'usr/libexec/QtWebEngineProcess', *list((stage / 'usr/lib').glob('*')), *list((stage / 'usr/plugins').rglob('*.so'))]:
    data = subprocess.run(['readelf', '--version-info', str(source)], capture_output=True, text=True).stdout
    for major, minor in re.findall(r'GLIBC_(\d+)\.(\d+)\b', data):
        version = max(version, (int(major), int(minor)))
metadata = ('AURORA/86 Linux x86_64\n'
            f'glibc mínima exigida pelas bibliotecas empacotadas: {version[0]}.{version[1]}\n'
            'Requer kernel Linux, glibc, sessão gráfica X11/Wayland e drivers gráficos do host.\n'
            'Qt, WebEngine/Chromium, recursos web, fontes e áudio incluídos.\n'
            'Lua 5.4 incorporado; programas e mods não exigem instalação separada.\n'
            'Sandbox Chromium preservada. Navegação requer acesso à rede.\n'
            'Bibliotecas fornecidas pelo host: ' + ', '.join(sorted(host_dependencies)) + '\n')
(stage / 'COMPATIBILITY.txt').write_text(metadata)
(root / 'outputs').mkdir(exist_ok=True)
(root / 'outputs/COMPATIBILIDADE.txt').write_text(metadata + '\nExecutar:\n  chmod +x AURORA86-x86_64.AppImage\n  ./AURORA86-x86_64.AppImage\n\nSem FUSE:\n  ./AURORA86-x86_64.AppImage --appimage-extract-and-run\n\nExtrair:\n  ./AURORA86-x86_64.AppImage --appimage-extract\n  ./squashfs-root/AppRun\n')
licence_root = stage / 'usr/share/licenses'
common = pathlib.Path('/usr/share/licenses/common')
if common.is_dir():
    shutil.copytree(common, licence_root / 'common', dirs_exist_ok=True)
packages = {'qt6-base', 'qt6-webengine', 'qt6-multimedia', 'ttf-dejavu'}
if shutil.which('pacman'):
    owners = subprocess.run(['pacman', '-Qoq', *[str(p) for p in seen]], capture_output=True, text=True)
    packages.update(owners.stdout.split())
    package_versions = subprocess.run(['pacman', '-Q', *sorted(packages)], capture_output=True, text=True)
    (licence_root / 'BUNDLED-PACKAGES.txt').write_text(package_versions.stdout)
for package in packages:
    source = pathlib.Path('/usr/share/licenses') / package
    if source.is_dir():
        shutil.copytree(source, licence_root / package, dirs_exist_ok=True)
shutil.copytree(root / 'third_party/doom', licence_root / 'doom',
                ignore=shutil.ignore_patterns('*.wad', '*.h'))
assets = root / 'assets'
for source in assets.glob('*LICENSE*'):
    shutil.copy2(source, licence_root / source.name)
for source in (root / 'packaging').glob('*LICENSE*'):
    shutil.copy2(source, licence_root / source.name)
for name in ('LICENSE', 'README.md', 'README.pt-BR.md'):
    if (root / name).is_file():
        shutil.copy2(root / name, stage / name)
shutil.copytree(root / 'mods', stage / 'mods')
for source in root.glob('AURORA86-*.png'):
    shutil.copy2(source, stage / source.name)
(licence_root / 'lua').mkdir()
for name in ('LICENSE.txt', 'UPSTREAM.txt'):
    shutil.copy2(root / 'third_party/lua' / name, licence_root / 'lua' / name)
(stage / 'usr/share/licenses/BUNDLED-LIBRARIES.txt').write_text('\n'.join(f'{soname} <- {source}' for soname, source in sorted(dependencies.items())) + '\n')
(licence_root / 'UPSTREAM-SOURCES.txt').write_text(
    'Dependências distribuídas sem alterações binárias, obtidas dos pacotes do host de build.\n'
    'Versões exatas: BUNDLED-PACKAGES.txt. Licenças: diretórios adjacentes.\n'
    'Fontes e receitas de build Arch Linux: https://gitlab.archlinux.org/archlinux/packaging/packages\n'
    'Qt: https://download.qt.io/official_releases/qt/\n'
    'Chromium: https://chromium.googlesource.com/chromium/src/\n'
    'FFmpeg: https://ffmpeg.org/releases/\n'
    'Lua 5.4.8: https://www.lua.org/ftp/lua-5.4.8.tar.gz\n'
    'Runtime AppImage 20251108: https://github.com/AppImage/type2-runtime/releases/tag/20251108\n'
    'Ferramenta AppImage 1.9.1: https://github.com/AppImage/appimagetool/releases/tag/1.9.1\n')
print(f'AppDir: {len(dependencies)} bibliotecas; glibc >= {version[0]}.{version[1]}')
PY
ARCH=x86_64 "$tools/squashfs-root/AppRun" --no-appstream --runtime-file "$tools/runtime-x86_64" \
    --mksquashfs-opt -processors --mksquashfs-opt 2 "$appdir" "$output"
chmod +x "$output"
(cd "$(dirname -- "$output")" && sha256sum "$(basename -- "$output")") > "$output.sha256"
printf 'AppImage criado: %s\n' "$output"

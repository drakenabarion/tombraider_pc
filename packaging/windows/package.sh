#!/usr/bin/env bash
# Builds dist/virtualon-windows/ (and dist/virtualon-windows.zip): Virtual-On.exe (the launcher,
# frozen with PyInstaller so players need no Python), Play Virtual-On.exe, bb-probe.exe with the
# MSYS2 CLANG64 DLLs it needs, the preparation scripts and run.py. Run from an MSYS2 CLANG64
# shell after `bash build.sh`. Freezing uses a Windows Python 3.10+ (python.org; WINPYTHON
# overrides) and a private venv in out/pyenv with PyInstaller and Pillow.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
source ./msys2-env.sh
[[ -f out/bb-probe.exe && -f out/bb-gpu-capabilities.exe && -f out/bb-play.exe ]] || { echo 'Build first: bash build.sh' >&2; exit 1; }

# A Windows Python (not MSYS2's) for PyInstaller.
python=${WINPYTHON:-}
if [[ -z $python ]]; then
    for candidate in /c/Python3*/python.exe "${LOCALAPPDATA:-/c/Users/$USER/AppData/Local}"/Programs/Python/Python3*/python.exe; do
        [[ -x $candidate ]] && python=$candidate
    done
fi
[[ -n $python ]] || { echo 'Need a Windows Python 3 (python.org) or WINPYTHON=path\to\python.exe' >&2; exit 1; }
# Windows Python needs USERPROFILE (some MSYS2 shells start without it).
export USERPROFILE=${USERPROFILE:-$(cygpath -w "/c/Users/$(id -un)")}
if [[ ! -x out/pyenv/Scripts/python.exe ]]; then
    "$python" -m venv out/pyenv
fi
out/pyenv/Scripts/python.exe -m pip install -q --disable-pip-version-check pyinstaller pillow
# The scripts run inside Virtual-On.exe (--script): the standard modules they import come along.
hidden=()
for module in argparse base64 collections hashlib json re shutil struct tempfile zlib ctypes.wintypes \
              virtualon_art uncharted_widgets bbport_steam prepare link_libc PIL.ImageTk; do
    hidden+=(--hidden-import "$module")
done
out/pyenv/Scripts/python.exe -m PyInstaller --noconfirm --clean --log-level WARN --windowed \
    --name Virtual-On --icon "$(cygpath -w "$PWD/launcher/virtualon.ico")" --distpath out/pyi-dist \
    --workpath out/pyi-work --specpath out/pyi-work --paths "$(cygpath -w "$PWD/scripts")" \
    --paths "$(cygpath -w "$PWD/launcher")" \
    "${hidden[@]}" \
    "$(cygpath -w "$PWD/launcher/virtualon_launcher.py")"

# The package is assembled in a fresh staging folder and zipped from there; dist/virtualon-windows
# (a playable copy that may hold saves and settings) is only refreshed afterwards.
dest=out/stage/virtualon-windows
rm -rf -- out/stage
mkdir -p "$dest/bin" "$dest/launcher" "$dest/scripts"
cp -r out/pyi-dist/Virtual-On/. "$dest/"
llvm-strip -o "$dest/Play Virtual-On.exe" out/bb-play.exe
cp launcher/virtualon.ico launcher/virtualon.png "$dest/launcher/"
# The executables without debug information (out/ keeps the symbols for crash reports).
for exe in bb-probe.exe bb-gpu-capabilities.exe; do
    llvm-strip --strip-debug -o "$dest/bin/$exe" "out/$exe"
done
# Every DLL the executables load from the CLANG64 tree (SDL3, FFmpeg, Vulkan loader, ...).
ldd "$dest/bin/bb-probe.exe" "$dest/bin/bb-gpu-capabilities.exe" |
    awk '/\/clang64\/bin\// {print $3}' | sort -u | while read -r dll; do
        cp -u "$dll" "$dest/bin/"
    done
# Only what run.py runs to prepare a game (Bloodborne's patch and mod tools stay out).
cp scripts/prepare.py scripts/link_modules.py scripts/link_libc.py scripts/content_profile.py "$dest/scripts/"
cp run.py LICENSE "$dest/"
cp packaging/windows/README-Virtual-On.txt "$dest/README.txt"
mkdir -p "$dest/licenses"
cp packaging/windows/licenses/*.txt "$dest/licenses/" 2>/dev/null || true
# No FSR 4 or DLSS files: anti-aliasing is not wired up for Virtual-On yet (the launcher keeps it off).
for license in gpu/third_party/wepoll/LICENSE:wepoll gpu/third_party/httplib/LICENSE:cpp-httplib; do
    if [[ -f ${license%%:*} ]]; then cp "${license%%:*}" "$dest/licenses/${license##*:}-LICENSE.txt"; fi
done
for package in protobuf abseil-cpp openssl miniupnpc zlib; do
    license=/clang64/share/licenses/$package/LICENSE
    if [[ -f $license ]]; then cp "$license" "$dest/licenses/$package-LICENSE.txt"; fi
done
find "$dest" -name __pycache__ -prune -exec rm -r {} +
mkdir -p dist
rm -f dist/virtualon-windows.zip
(cd out/stage && powershell -NoProfile -Command \
    "Compress-Archive -Path virtualon-windows -DestinationPath ../../dist/virtualon-windows.zip")

# Refresh dist/virtualon-windows, keeping what players create there (saves, settings), and only
# while nothing runs from it: deleting a running launcher's files breaks it.
play=dist/virtualon-windows
running=$(powershell -NoProfile -Command \
    "@(Get-Process | Where-Object { \$_.Path -like '$(cygpath -w "$PWD/$play")\\*' }).Count" | tr -d '\r')
if [[ ${running:-0} != 0 ]]; then
    echo "dist/virtualon-windows is in use ($running processes): not refreshed; the zip is ready." >&2
else
    mkdir -p "$play"
    find "$play" -mindepth 1 -maxdepth 1 ! -name user ! -name out ! -name art ! -name bbport.ini \
        ! -name input.ini ! -name last_run.log -exec rm -rf {} +
    cp -r "$dest/." "$play/"
fi
du -sh "$dest" dist/virtualon-windows.zip

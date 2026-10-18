#!/usr/bin/env bash
# Run from the MSYS2 CLANG64 shell (build-windows.bat does that): installs the toolchain (first
# run), builds the port and packages it into dist/virtualon-windows/ (Virtual-On.exe).
# The kit folder holds BuildEnv/, Game/ and the log. It defaults to the folder that contains the
# repo (Virtual-On-Windows/ beside virtualon_pc/); VO_KIT overrides it.
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
kit=${VO_KIT:-$(dirname -- "$repo")}
[[ $kit == *:* ]] && kit=$(cygpath -u "$kit")
[[ ${MSYSTEM:-} == CLANG64 ]] || { echo 'Open the "MSYS2 CLANG64" shell, not MSYS/UCRT64.' >&2; exit 1; }
# The Windows Python for PyInstaller: BuildEnv\python.exe in the kit unless WINPYTHON is set.
if [[ -z ${WINPYTHON:-} && -x $kit/BuildEnv/python.exe ]]; then export WINPYTHON=$kit/BuildEnv/python.exe; fi
pacman -S --needed git patch unzip \
  mingw-w64-clang-x86_64-{toolchain,cmake,ninja,pkgconf,python} \
  mingw-w64-clang-x86_64-{vulkan-headers,vulkan-loader,vulkan-memory-allocator,glslang} \
  mingw-w64-clang-x86_64-{sdl3,fmt,boost,robin-map,xxhash,ffmpeg,zydis} \
  mingw-w64-clang-x86_64-{spirv-tools,spirv-headers,spirv-cross} \
  mingw-w64-clang-x86_64-{protobuf,openssl,zlib,miniupnpc,nlohmann-json}
# exFAT has no owners or permission bits: let git use this copy as it is.
git config --global --add safe.directory '*'
git -C "$repo" config core.filemode false
cd "$repo"
bash build.sh 2>&1 | tee "$kit/build-log.txt"
# Packaging installs PyInstaller and Pillow from PyPI into out/pyenv the first time.
if [[ ! -x out/pyenv/Scripts/python.exe ]]; then
    read -r -p 'Packaging downloads PyInstaller and Pillow from PyPI into out/pyenv. Continue? [y/N] ' answer
    [[ $answer == [yY]* ]] || { echo 'Stopped before packaging: the build is in out/.'; exit 0; }
fi
bash packaging/windows/package.sh 2>&1 | tee -a "$kit/build-log.txt"
echo
echo "Done: $repo/dist/virtualon-windows/Virtual-On.exe"
echo "Game folder for the launcher: $kit/Game"

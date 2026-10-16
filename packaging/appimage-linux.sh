#!/usr/bin/env bash
# Builds dist/Uncharted-x86_64.AppImage from the current Linux build (build first: out/bb-probe,
# out/gpu/libbbgpu.so). No Nix: the launcher is frozen with PyInstaller, and the libraries the
# game needs are copied from this system (graphics driver, glibc, audio and windowing stay the
# host's). Laid out like the GOW III AppImage.
# Needs: python3 with tkinter, patchelf, network for pip and appimagetool on the first run.
# Data (prepared executables, bbport.ini, input.ini): ~/.local/share/uncharted (BB_DATA_DIR).
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
root=$PWD
[[ -f out/bb-probe && -f out/gpu/libbbgpu.so ]] || { echo 'Build first (out/bb-probe, out/gpu/libbbgpu.so).' >&2; exit 1; }
command -v patchelf >/dev/null || { echo 'patchelf missing: sudo dnf install patchelf' >&2; exit 1; }
python3 -c 'import tkinter' 2>/dev/null || { echo 'python3 tkinter missing: sudo dnf install python3-tkinter' >&2; exit 1; }

# 1. The launcher, frozen (it runs run.py and the scripts itself: --run / --script).
if [[ ! -x out/pyenv-linux/bin/python ]]; then
    python3 -m venv out/pyenv-linux
fi
py=out/pyenv-linux/bin/python
"$py" -m pip install -q --disable-pip-version-check pyinstaller pillow
hidden=()
for module in argparse base64 collections hashlib json re shutil struct tempfile zlib \
              uncharted_art uncharted_fonts uncharted_widgets bbport_steam PIL.ImageTk PIL._tkinter_finder; do
    hidden+=(--hidden-import "$module")
done
"$py" -m PyInstaller --noconfirm --clean --log-level WARN --windowed \
    --name uncharted-launcher --distpath out/pyi-dist-linux --workpath out/pyi-work-linux \
    --specpath out/pyi-work-linux --paths "$root/scripts" --paths "$root/launcher" \
    --add-data "$root/launcher/fonts:fonts" --add-data "$root/launcher/covers:covers" \
    "${hidden[@]}" launcher/uncharted_launcher.py

# 2. AppDir.
app=out/AppDir
rm -rf -- "$app"
share=$app/usr/share/uncharted
lib=$app/usr/lib
mkdir -p "$share/bin" "$share/scripts" "$share/launcher" "$lib"
cp -r out/pyi-dist-linux/uncharted-launcher/. "$share/"
cp out/bb-probe "$share/bin/"
cp out/gpu/libbbgpu.so "$lib/"
# DLSS (a build with DLSS_SDK_ROOT): the bridge and NVIDIA's libraries beside bb-probe, where the
# port and NGX look for them; NVIDIA's files are copied unmodified.
if [[ -f out/libbbport_dlss.so ]]; then
    cp out/libbbport_dlss.so out/libnvidia-ngx-dlss*.so.* "$share/bin/"
    [[ -f out/NVIDIA-DLSS-LICENSE.txt ]] && cp out/NVIDIA-DLSS-LICENSE.txt "$share/"
fi
cp scripts/prepare.py scripts/link_modules.py scripts/link_libc.py scripts/content_profile.py \
   scripts/patches.py scripts/mods.py "$share/scripts/"
cp run.py LICENSE "$share/"
[[ -d patches ]] && cp -r patches "$share/"
[[ -d fsr4_shaders ]] && cp -r fsr4_shaders "$share/"
"$py" - "$root/launcher/uncharted.ico" "$share/launcher/uncharted.png" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1])
sizes = sorted(im.info.get('sizes', [im.size]), key=lambda s: s[0])
if sizes:
    im.size = sizes[-1]
im.convert('RGBA').resize((256, 256)).save(sys.argv[2])
PY
cp "$share/launcher/uncharted.png" "$app/uncharted.png"

# 3. Libraries: everything bb-probe and libbbgpu.so load, minus what must come from the host.
# Exact names (libc must not catch libcrypto) and whole families (libX*, libxcb*, libGL*, ...).
exact='^(linux-vdso|ld-linux-x86-64|libc|libm|libdl|libpthread|librt|libresolv|libutil|libanl|libmvec|libstdc\+\+|libgcc_s|libvulkan|libdrm|libgbm|libasound|libpulse|libpulse-simple|libjack|libdbus-1|libudev|libsystemd|libfontconfig|libfreetype|libexpat|libz|libuuid|libcap|libselinux|libgcrypt|libgpg-error)\.so'
family='^(libGL|libEGL|libOpenGL|libnvidia|libX|libxcb|libxkbcommon|libwayland|libdecor|libpipewire)'
for elf in out/bb-probe out/gpu/libbbgpu.so; do
    ldd "$elf" | awk '/=> \//{print $1, $3}'
done | sort -u | while read -r name path; do
    [[ $name == libbbgpu.so ]] && continue
    [[ $name =~ $exact || $name =~ $family ]] && continue
    cp -L "$path" "$lib/$name"
done
# Search paths inside the AppImage instead of LD_LIBRARY_PATH, which would leak into xdg-open,
# kdialog and the other programs the launcher starts.
patchelf --set-rpath '$ORIGIN/../../../lib' "$share/bin/bb-probe"
for so in "$lib"/*.so*; do
    chmod u+w "$so"
    patchelf --set-rpath '$ORIGIN' "$so"
done

# 4. Entry point and desktop file.
cat > "$app/AppRun" <<'RUN'
#!/usr/bin/env bash
# AppImage entry point: the frozen Tk launcher. Usage: Uncharted-x86_64.AppImage [--play]
set -euo pipefail
here=$(dirname -- "$(readlink -f -- "$0")")
share=$here/usr/share/uncharted
export BB_DATA_DIR=${BB_DATA_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/uncharted}
mkdir -p "$BB_DATA_DIR"
cd -- "$share"
exec "$share/uncharted-launcher" "$@"
RUN
chmod +x "$app/AppRun"
cat > "$app/uncharted.desktop" <<'DESK'
[Desktop Entry]
Type=Application
Name=Uncharted: The Nathan Drake Collection
Comment=PC port of the Uncharted collection (bring your own game dump)
Exec=AppRun
Icon=uncharted
Categories=Game;
Terminal=false
StartupWMClass=uncharted
DESK

# 5. Pack.
tool=out/appimagetool-x86_64.AppImage
if [[ ! -x $tool ]]; then
    curl -fL -o "$tool" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "$tool"
fi
mkdir -p dist
ARCH=x86_64 "$tool" --no-appstream "$app" dist/Uncharted-x86_64.AppImage
ls -lh dist/Uncharted-x86_64.AppImage
echo "Bundled libraries:"; ls "$lib"

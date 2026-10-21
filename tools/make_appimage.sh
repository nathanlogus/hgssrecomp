#!/bin/bash
# Wraps the setup executable from tools/package_setup.py into an AppImage.
#
#   tools/make_appimage.sh <johto-online-setup binary> <arch: x86_64|aarch64> <output.AppImage>
#
# Needs appimagetool (APPIMAGETOOL=path, default: on PATH) and, when building for
# another architecture, that architecture's AppImage runtime (RUNTIME=path, from
# https://github.com/AppImage/type2-runtime/releases).
set -euo pipefail

BIN="$1"
ARCH="$2"
OUT="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"
APPDIR="$(mktemp -d)/johto-online-setup.AppDir"

mkdir -p "$APPDIR/usr/bin"
install -m755 "$BIN" "$APPDIR/usr/bin/johto-online-setup"
cp "$HERE/appimage/johto-online-setup.png" "$APPDIR/johto-online-setup.png"
cp "$HERE/appimage/johto-online-setup.png" "$APPDIR/.DirIcon"
cat > "$APPDIR/johto-online-setup.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Johto Online setup
Comment=Builds the SoulSilver recompilation from your own ROM
Exec=johto-online-setup
Icon=johto-online-setup
Terminal=false
Categories=Game;
EOF
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
exec "$(dirname "$(readlink -f "$0")")/usr/bin/johto-online-setup" "$@"
EOF
chmod +x "$APPDIR/AppRun"

args=()
[ -n "${RUNTIME:-}" ] && args+=(--runtime-file "$RUNTIME")
APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$ARCH" "${APPIMAGETOOL:-appimagetool}" --no-appstream "${args[@]}" "$APPDIR" "$OUT"
rm -rf "$(dirname "$APPDIR")"

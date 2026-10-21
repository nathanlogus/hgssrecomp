#!/bin/bash
# One-step setup: gets the decompilation, builds the game with it, generates the
# recompiled code and compiles everything. Safe to run again: steps that are already
# done are quick, and only what changed is rebuilt.
#
#   ./setup.sh                 do everything (asks before installing packages)
#   ./setup.sh --deps          install the required packages without asking (uses sudo)
#   ./setup.sh --decomp DIR    use an existing pokeheartgold checkout instead of ../pokeheartgold
#   ./setup.sh --rom FILE      also check FILE (a dump of your own cartridge) against the build
#   ./setup.sh --jobs N        compile with N parallel jobs (default: from CPU count and RAM)
#   ./setup.sh --desktop       add a menu entry for the game
set -euo pipefail

if [ "$(uname -s)" = Darwin ]; then
    echo 'On macOS use: python3 johto_online_setup.py --rom /path/to/your-rom.nds' >&2
    echo 'See README.md for Xcode Command Line Tools and Homebrew prerequisites.' >&2
    exit 1
fi

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

DECOMP="$HERE/../pokeheartgold"
DUMP=""
JOBS=""
AUTO_DEPS=0
DESKTOP=0
SHA1_SS="f8dc38ea20c17541a43b58c5e6d18c1732c7e582"   # Pokemon SoulSilver (USA)

while [ $# -gt 0 ]; do
    case "$1" in
        --deps) AUTO_DEPS=1 ;;
        --decomp) DECOMP="$2"; shift ;;
        --rom) DUMP="$2"; shift ;;
        --jobs) JOBS="$2"; shift ;;
        --desktop) DESKTOP=1 ;;
        -h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1 (see ./setup.sh --help)" >&2; exit 1 ;;
    esac
    shift
done

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\n\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
say "Checking the tools and libraries"

missing=()
for c in git make cmake ninja python3 pkg-config c++ arm-none-eabi-gcc bison flex; do
    command -v "$c" >/dev/null 2>&1 || missing+=("$c")
done
pkg-config --exists sdl2 2>/dev/null || missing+=("SDL2 (development files)")
pkg-config --exists libpng 2>/dev/null || missing+=("libpng (development files)")
pkg-config --exists zlib 2>/dev/null || missing+=("zlib (development files)")
python3 -c "import elftools" 2>/dev/null || missing+=("pyelftools (Python)")

if [ ${#missing[@]} -gt 0 ]; then
    echo "Missing: ${missing[*]}"
    if [ -e /run/ostree-booted ]; then
        die "This looks like an immutable system (Bazzite, Silverblue, SteamOS...). Install the
packages inside a container instead and run this script there, e.g.:
    distrobox create -n johto-online -i ubuntu:24.04
    distrobox enter johto-online -- ./setup.sh --deps
The finished program also runs outside the container."
    fi
    if command -v apt-get >/dev/null; then
        install=(bash -c "sudo dpkg --add-architecture i386 && sudo apt-get update && sudo apt-get install -y \
            bison flex g++ gcc-arm-none-eabi git make cmake ninja-build pkg-config wget python3 python3-pyelftools \
            xz-utils nasm libc6:i386 libpng-dev libsdl2-dev zlib1g-dev")
    elif command -v dnf >/dev/null; then
        install=(sudo dnf install -y arm-none-eabi-gcc-cs bison flex gcc-c++ git make cmake ninja-build python3 \
            python3-pyelftools wget2 xz glibc32 libpng-devel SDL2-devel zlib-devel pkgconf-pkg-config)
    elif command -v pacman >/dev/null; then
        install=(sudo pacman -S --needed arm-none-eabi-gcc bison flex gcc git make cmake ninja python \
            python-pyelftools wget xz lib32-glibc libpng sdl2 zlib pkgconf)
        echo "(Arch: the multilib repository has to be enabled for lib32-glibc.)"
    else
        die "Unknown package manager. Install the packages listed under \"Building\" in README.md and run this again."
    fi
    if [ $AUTO_DEPS -eq 0 ]; then
        echo "This will run:"
        echo "    ${install[*]}"
        read -r -p "Install them now? [y/N] " answer
        [[ "$answer" =~ ^[Yy] ]] || die "Install the missing packages and run this again."
    fi
    "${install[@]}"
fi

# ---------------------------------------------------------------------------
say "Getting the decompilation (pret/pokeheartgold)"

if [ ! -d "$DECOMP/.git" ] && [ ! -f "$DECOMP/Makefile" ]; then
    git clone https://github.com/pret/pokeheartgold.git "$DECOMP"
else
    echo "Using $DECOMP"
fi
DECOMP="$(cd "$DECOMP" && pwd)"

# ---------------------------------------------------------------------------
say "Building SoulSilver from the decompilation (the first time takes a while)"

make -C "$DECOMP" soulsilver -j"$(nproc)"
BUILD_DIR="$DECOMP/build/soulsilver.us"
ROM="$BUILD_DIR/pokesoulsilver.us.nds"
[ -f "$ROM" ] || die "The decompilation build did not produce $ROM"

sum="$(sha1sum "$ROM" | cut -d' ' -f1)"
if [ "$sum" != "$SHA1_SS" ]; then
    die "The built ROM ($sum) is not identical to the retail game ($SHA1_SS).
The recompiled code would not match it. Check the decompilation build (its INSTALL.md)."
fi
echo "Built ROM matches the retail cartridge."

if [ -n "$DUMP" ]; then
    dsum="$(sha1sum "$DUMP" | cut -d' ' -f1)"
    [ "$dsum" = "$SHA1_SS" ] || die "$DUMP is not Pokemon SoulSilver (USA): SHA-1 $dsum, expected $SHA1_SS"
    echo "Your dump $DUMP is the same game."
fi

# ---------------------------------------------------------------------------
say "Generating the recompiled code (gen/: the game's code, keep it to yourself)"

python3 tools/recomp.py "$BUILD_DIR" melonDS/src gen
python3 tools/apps.py "$BUILD_DIR" gen

# ---------------------------------------------------------------------------
if [ -z "$JOBS" ]; then
    # The generated sources need about 1.5 GB of memory each to compile.
    mem_kb="$(awk '/MemAvailable/ {print $2}' /proc/meminfo 2>/dev/null || echo 8000000)"
    JOBS=$(( mem_kb / 1500000 ))
    [ "$JOBS" -ge 1 ] || JOBS=1
    [ "$JOBS" -le "$(nproc)" ] || JOBS="$(nproc)"
fi
say "Compiling with $JOBS jobs (the first time takes 20-40 minutes)"

[ -f build/build.ninja ] || cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build -j"$JOBS"

# run.sh finds the ROM through this file.
printf '%s\n' "${DUMP:-$ROM}" > johto-online.rom

# ---------------------------------------------------------------------------
if [ $DESKTOP -eq 1 ]; then
    mkdir -p "$HOME/.local/share/applications"
    cat > "$HOME/.local/share/applications/johto-online.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Johto Online
Exec="$HERE/run.sh" --fullscreen
Path=$HERE
Terminal=false
Categories=Game;
EOF
    echo "Added a menu entry: Johto Online"
fi

say "Done"
echo "Start the game with:"
echo "    ./run.sh --fullscreen"

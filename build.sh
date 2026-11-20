#!/bin/sh
set -eu

: "${MPC_SDK:?Set MPC_SDK to an Akai MPC Plugin Builder SDK checkout}"
: "${ZIG:=zig}"
: "${PYTHON:=python3}"

project=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
zig_bin=$(command -v "$ZIG")
: "${ZIG_GLOBAL_CACHE_DIR:=$project/build/zig-global}"
: "${ZIG_LOCAL_CACHE_DIR:=$project/build/zig-local}"
export ZIG_GLOBAL_CACHE_DIR ZIG_LOCAL_CACHE_DIR
mkdir -p "$project/build/deps" "$project/Content"
header="$project/build/deps/json.hpp"
if [ ! -f "$header" ]; then
    curl --fail --silent --show-error --location \
        https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp \
        --output "$header.part"
    mv "$header.part" "$header"
fi
"$PYTHON" - "$header" <<'PY'
import hashlib
import pathlib
import sys

expected = "9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6"
if hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest() != expected:
    sys.exit("nlohmann/json header has the wrong checksum")
PY

cp "$MPC_SDK/LICENSE" "$project/Content/Akai-SDK-LICENSE.txt"
cp "$project/helper/nlohmann-json-LICENSE.txt" "$project/Content/nlohmann-json-LICENSE.txt"
"$zig_bin" c++ -target arm-linux-musleabihf -mcpu=cortex_a17 -std=c++17 -O2 -static -s \
    -I"$project/build/deps" "$project/helper/Refresh.cpp" -o "$project/Content/template-refresh-arm32"
"$zig_bin" c++ -target aarch64-linux-musl -std=c++17 -O2 -static -s \
    -I"$project/build/deps" "$project/helper/Refresh.cpp" -o "$project/Content/template-refresh-arm64"

export TEMPLATE_CHOOSER_ZIG="$zig_bin"
sdk_compiler="$project/build/zig-sdk"
cat > "$sdk_compiler" <<'SH'
#!/bin/sh
if [ "$1" = c++ ]; then
    shift
    for arg in "$@"; do
        if [ "$arg" = -target ]; then
            # On MPC X, unloading the ARM C++ library can leave a dangling
            # runtime exit callback. The host still closes each plugin instance.
            exec "$TEMPLATE_CHOOSER_ZIG" c++ "$@" -Wl,-z,nodelete
        fi
    done
    if [ "$(uname -s)" = Darwin ]; then
        exec /usr/bin/clang++ "$@"
    fi
    exec "$TEMPLATE_CHOOSER_ZIG" c++ "$@"
fi
exec "$TEMPLATE_CHOOSER_ZIG" "$@"
SH
chmod +x "$sdk_compiler"
"$PYTHON" "$project/ui/render_guide.py" "$project/assets/chooser-guide.png"
"$PYTHON" "$MPC_SDK/tools/build.py" "$project" --target all --zig "$sdk_compiler"
"$PYTHON" "$MPC_SDK/tools/package.py" "$project"

#!/bin/sh
# Checks that an engine installed for Linux (cmake --install <build> --prefix <folder>) holds what it
# needs to run on its own: the programs, the engine library, the resources, the shaders of the
# engine, the compiler of the projects' shaders, the C# runtime and the package that builds the C++
# of games. An engine whose build failed installs next to nothing: the CI runs this before it keeps
# the engine as a download.
#
#   scripts/check_engine_package.sh <folder>
set -eu

if [ $# -ne 1 ] || [ ! -d "$1" ]; then
    echo "usage: $0 <installed engine folder>" >&2
    exit 2
fi
root=$1
missing=0

fail() {
    echo "missing: $1" >&2
    missing=$((missing + 1))
}

# A file, executable when the second argument says so.
need() {
    if [ ! -f "$root/$1" ]; then
        fail "$1"
    elif [ "${2:-}" = executable ] && [ ! -x "$root/$1" ]; then
        fail "$1 (not executable)"
    fi
}

# At least one file of a folder named as the pattern says.
need_any() {
    if [ -z "$(find "$root/$1" -name "$2" -type f 2>/dev/null | head -n 1)" ]; then
        fail "$1/$2"
    fi
}

need bin/devex-editor executable
need bin/devex-player executable
need bin/devex-bindgen executable
need lib/libdevex-engine.so
need bin/slang/bin/slangc executable
need_any bin/slang/lib "libslang-compiler.so*"
need_any bin/shaders "*.spv"
need_any bin/shaders/modules "*.slang"
need_any bin/resources/fonts "*.ttf"
need bin/resources/icons/devex.png
need bin/managed/Devex.Managed.dll
need cmake/DevexConfig.cmake
need cmake/DevexGameModule.cmake
need include/devex/runtime/Game.hpp
need_any include/glm "*.hpp"

# The programs find the engine library beside them ($ORIGIN/../lib), and the rest on the system.
for program in bin/devex-editor bin/devex-player bin/slang/bin/slangc; do
    if [ -x "$root/$program" ]; then
        unresolved=$(ldd "$root/$program" 2>&1 | grep "not found" || true)
        if [ -n "$unresolved" ]; then
            fail "libraries of $program: $unresolved"
        fi
    fi
done

if [ "$missing" -ne 0 ]; then
    echo "The engine in $root is incomplete: $missing missing." >&2
    exit 1
fi
echo "The engine in $root is complete."

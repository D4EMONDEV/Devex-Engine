# The ports of the web player with threads (preset web-threads): those of wasm32-emscripten, compiled with -pthread, since
# WebAssembly does not link code that shares its memory with code that does not. The overlay port of SDL3 gives SDL its
# threads when DEVEX_WASM_THREADS is on.
include("${CMAKE_CURRENT_LIST_DIR}/wasm32-emscripten.cmake")

set(DEVEX_WASM_THREADS ON)
string(APPEND VCPKG_C_FLAGS " -pthread")
string(APPEND VCPKG_CXX_FLAGS " -pthread")
string(APPEND VCPKG_LINKER_FLAGS " -pthread")

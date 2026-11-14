# vcpkg's community triplet for the web, as it comes, with what Devex needs on top. See the web in docs/decisions.md.
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED EMSCRIPTEN_ROOT EMSDK PATH)

if(NOT DEFINED ENV{EMSCRIPTEN_ROOT})
   find_path(EMSCRIPTEN_ROOT "emcc")
else()
   set(EMSCRIPTEN_ROOT "$ENV{EMSCRIPTEN_ROOT}")
endif()

if(NOT EMSCRIPTEN_ROOT)
   if(NOT DEFINED ENV{EMSDK})
      message(FATAL_ERROR "The emcc compiler not found in PATH")
   endif()
   set(EMSCRIPTEN_ROOT "$ENV{EMSDK}/upstream/emscripten")
endif()

if(NOT EXISTS "${EMSCRIPTEN_ROOT}/cmake/Modules/Platform/Emscripten.cmake")
   message(FATAL_ERROR "Emscripten.cmake toolchain file not found")
endif()

set(VCPKG_TARGET_ARCHITECTURE wasm32)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Emscripten)
# Chainload vcpkg's wrapper toolchain rather than Emscripten.cmake directly:
# the wrapper includes Emscripten.cmake and then applies VCPKG_C(XX)_FLAGS
# and VCPKG_LINKER_FLAGS, which would otherwise be silently dropped.
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${VCPKG_ROOT_DIR}/scripts/toolchains/emscripten.cmake")

# Emscripten's library of C++ no longer brings <iterator> along with the other headers, and several libraries counted on
# it: simdjson (under fastgltf) for std::inserter, clipper2 (under manifold) for std::back_inserter. Every port gets it.
# vcpkg wants the flags of C set whenever those of C++ are.
set(VCPKG_C_FLAGS "${VCPKG_C_FLAGS}")
string(APPEND VCPKG_CXX_FLAGS " -include iterator")

# Exceptions as Devex builds them, native to WebAssembly: a library that catches none still lets them through.
string(APPEND VCPKG_C_FLAGS " -fwasm-exceptions")
string(APPEND VCPKG_CXX_FLAGS " -fwasm-exceptions")
string(APPEND VCPKG_LINKER_FLAGS " -fwasm-exceptions")

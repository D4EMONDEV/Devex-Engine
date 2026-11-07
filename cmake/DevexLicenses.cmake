# The licenses of what Devex ships, in bin/licenses: one file for each library, font and tool. They go with the installed
# engine and with every game it exports, as most of these licenses ask of every copy.
set(_devex_licenses "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses")
file(REMOVE_RECURSE "${_devex_licenses}")

function(_devex_license name file)
    configure_file("${file}" "${_devex_licenses}/${name}.txt" COPYONLY)
endfunction()

_devex_license(README "${PROJECT_SOURCE_DIR}/third_party/LICENSES-README.txt")
_devex_license(devex "${PROJECT_SOURCE_DIR}/LICENSE")
_devex_license(inter "${PROJECT_SOURCE_DIR}/third_party/fonts/Inter-OFL.txt")
_devex_license(jetbrains-mono "${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMono-OFL.txt")
_devex_license(noto-sans "${PROJECT_SOURCE_DIR}/third_party/fonts/NotoSans-OFL.txt")
_devex_license(lucide "${PROJECT_SOURCE_DIR}/third_party/lucide/LICENSE")
_devex_license(ufbx "${PROJECT_SOURCE_DIR}/third_party/ufbx/LICENSE")
_devex_license(sdl-android "${PROJECT_SOURCE_DIR}/third_party/sdl-android/LICENSE.txt")
_devex_license(slang "${PROJECT_SOURCE_DIR}/third_party/slang/LICENSE")
# The runtime that a game in C# carries.
_devex_license(dotnet "${PROJECT_SOURCE_DIR}/third_party/dotnet/LICENSE.txt")
_devex_license(dotnet-third-party-notices "${PROJECT_SOURCE_DIR}/third_party/dotnet/THIRD-PARTY-NOTICES.txt")

# What vcpkg built, whatever this system and its features brought in. The tests and the tools of the build do not ship.
set(_devex_ports "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
file(GLOB _devex_port_licenses "${_devex_ports}/*/copyright")
if(NOT _devex_port_licenses)
    message(FATAL_ERROR "No license of the libraries of vcpkg in ${_devex_ports}: they have to ship with the engine")
endif()
foreach(_devex_port_license IN LISTS _devex_port_licenses)
    get_filename_component(_devex_port "${_devex_port_license}" DIRECTORY)
    get_filename_component(_devex_port "${_devex_port}" NAME)
    if(NOT _devex_port MATCHES "^(catch2|vcpkg-.*)$")
        _devex_license("${_devex_port}" "${_devex_port_license}")
    endif()
endforeach()

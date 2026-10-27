# Devex: the port of vcpkg's builtin-baseline (sdl3 3.4.16), with what Linux desktops need built in:
# - the title bars that libdecor draws, which GNOME leaves to Wayland windows (KDE and others draw
#   their own through xdg-decoration, which SDL prefers);
# - the X11 extensions: raw mouse motion and smooth scrolling (XInput2), the cursor theme (Xcursor),
#   display modes (XRandR), confined pointers (XFixes)...
# vcpkg leaves them out, so that the libraries of the build machine never change what SDL can do;
# here they are required at build time, and SDL loads them when the program starts if the machine
# has them, as it does X11 and Wayland themselves.

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO libsdl-org/SDL
    REF "release-${VERSION}"
    SHA512 d815b28f72d60e4d83e31d878bcf2dd52089353637a4ffd0fd094deaafaaa5b5ee797caad9eeb952f6c28a2bbe26253092c294ce1aefbeb92d85469f35229ed1
    HEAD_REF main
    PATCHES
        fix-freebsd.patch
)

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "static" SDL_STATIC)
string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" SDL_SHARED)
string(COMPARE EQUAL "${VCPKG_CRT_LINKAGE}" "static" FORCE_STATIC_VCRT)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        alsa     SDL_ALSA
        dbus     SDL_DBUS
        ibus     SDL_IBUS
        vulkan   SDL_VULKAN
        wayland  SDL_WAYLAND
        x11      SDL_X11
        libusb   SDL_HIDAPI_LIBUSB
)

if (VCPKG_TARGET_IS_EMSCRIPTEN)
    vcpkg_check_features(OUT_FEATURE_OPTIONS EMSCRIPTEN_FEATURE_OPTIONS
        FEATURES
            emscripten-pthreads     SDL_PTHREADS
    )
    vcpkg_list(APPEND FEATURE_OPTIONS "${EMSCRIPTEN_FEATURE_OPTIONS}")
endif()

if ("x11" IN_LIST FEATURES)
    message(WARNING "You will need to install Xorg dependencies to use feature x11:\nsudo apt install libx11-dev libxft-dev libxext-dev libxcursor-dev libxi-dev libxrandr-dev libxfixes-dev libxss-dev libxtst-dev\n")
endif()
if ("wayland" IN_LIST FEATURES)
    message(WARNING "You will need to install Wayland dependencies to use feature wayland:\nsudo apt install libwayland-dev libxkbcommon-dev libegl1-mesa-dev libdecor-0-dev\n")
endif()
if ("ibus" IN_LIST FEATURES)
    message(WARNING "You will need to install ibus dependencies to use feature ibus:\nsudo apt install libibus-1.0-dev\n")
endif()

if ("libusb" IN_LIST FEATURES)
    if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
        vcpkg_list(APPEND FEATURE_OPTIONS "-DSDL_HIDAPI_LIBUSB_SHARED=ON")
    else()
        vcpkg_list(APPEND FEATURE_OPTIONS "-DSDL_HIDAPI_LIBUSB_SHARED=OFF")
    endif()
endif()

if(VCPKG_TARGET_IS_LINUX AND "wayland" IN_LIST FEATURES)
    set(WAYLAND_LIBDECOR ON)
else()
    set(WAYLAND_LIBDECOR OFF)
endif()
if("x11" IN_LIST FEATURES)
    set(X11_EXTENSIONS ON)
else()
    set(X11_EXTENSIONS OFF)
endif()

# option for not need to show windows
list(APPEND FEATURE_OPTIONS -DSDL_UNIX_CONSOLE_BUILD=ON)
if (VCPKG_TARGET_IS_LINUX AND NOT "x11" IN_LIST FEATURES AND NOT "wayland" IN_LIST FEATURES)
    message(WARNING "The selected features don't allow sdl3 to create windows, which is usually unintentional. You can get windowing support by installing the x11 and/or wayland features.")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DSDL_STATIC=${SDL_STATIC}
        -DSDL_SHARED=${SDL_SHARED}
        -DSDL_FORCE_STATIC_VCRT=${FORCE_STATIC_VCRT}
        -DSDL_LIBC=ON
        # Prevent host-installed Unix libraries from silently changing SDL's capabilities.
        -DSDL_FRIBIDI=OFF
        -DSDL_JACK=OFF
        -DSDL_KMSDRM=OFF
        -DSDL_LIBTHAI=OFF
        -DSDL_LIBUDEV=OFF
        -DSDL_LIBURING=OFF
        -DSDL_PIPEWIRE=OFF
        -DSDL_PULSEAUDIO=OFF
        -DSDL_ROCKCHIP=OFF
        -DSDL_RPI=OFF
        -DSDL_SNDIO=OFF
        -DSDL_WAYLAND_LIBDECOR=${WAYLAND_LIBDECOR}
        -DSDL_TEST_LIBRARY=OFF
        -DSDL_TESTS=OFF
        -DSDL_X11_XCURSOR=${X11_EXTENSIONS}
        -DSDL_X11_XDBE=${X11_EXTENSIONS}
        -DSDL_X11_XFIXES=${X11_EXTENSIONS}
        -DSDL_X11_XINPUT=${X11_EXTENSIONS}
        -DSDL_X11_XRANDR=${X11_EXTENSIONS}
        -DSDL_X11_XSHAPE=${X11_EXTENSIONS}
        -DSDL_X11_XSCRNSAVER=${X11_EXTENSIONS}
        -DSDL_X11_XSYNC=${X11_EXTENSIONS}
        -DSDL_X11_XTEST=${X11_EXTENSIONS}
        -DSDL_INSTALL_CMAKEDIR_ROOT=share/${PORT}
        # Specifying the revision skips the need to use git to determine a version
        -DSDL_REVISION=vcpkg
    MAYBE_UNUSED_VARIABLES
        SDL_FORCE_STATIC_VCRT
)

# SDL only warns when it misses libdecor, and its windows would then have no title bar on GNOME.
if(WAYLAND_LIBDECOR)
    file(GLOB build_configs "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-*/include-config-*/build_config/SDL_build_config.h")
    foreach(build_config IN LISTS build_configs)
        file(STRINGS "${build_config}" libdecor REGEX "^#define HAVE_LIBDECOR_H")
        if(NOT libdecor)
            message(FATAL_ERROR "SDL did not find libdecor, which draws the title bars of windows on GNOME:\nsudo apt install libdecor-0-dev\n")
        endif()
    endforeach()
endif()

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(
    FILE_LIST
        "${SOURCE_PATH}/LICENSE.txt"
        "${SOURCE_PATH}/src/hidapi/LICENSE-bsd.txt"
        "${SOURCE_PATH}/src/video/stb_image.h"
        "${SOURCE_PATH}/src/video/yuv2rgb/LICENSE"
        "${SOURCE_PATH}/include/SDL3/SDL_opengles2_gl2.h"
        "${SOURCE_PATH}/include/SDL3/SDL_opengles2_gl2platform.h"
    COMMENT "SDL_opengles2_gl2platform.h is licensed under Apache-2.0; see https://www.apache.org/licenses/LICENSE-2.0."
)

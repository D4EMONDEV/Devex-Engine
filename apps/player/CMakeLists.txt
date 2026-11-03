if(ANDROID)
    # The activity of SDL loads libmain.so and calls its SDL_main.
    add_library(devex_player SHARED
        src/main.cpp
    )
    find_package(SDL3 CONFIG REQUIRED)
    target_link_libraries(devex_player PRIVATE SDL3::Headers)
else()
    add_executable(devex_player
        src/main.cpp
    )
    add_executable(Devex::Player ALIAS devex_player)
endif()

target_link_libraries(devex_player PRIVATE Devex::Engine)
set_target_properties(devex_player PROPERTIES
    OUTPUT_NAME "devex-player"
    FOLDER "Applications"
)
if(ANDROID)
    set_target_properties(devex_player PROPERTIES OUTPUT_NAME "main")

    # The Android project of the player, put together in the build folder: the template of android/,
    # the Java side of SDL, the libraries with the shared C++ runtime, and the files of the engine,
    # which the activity extracts. `gradle assembleDebug` there makes an APK, once a game has put its
    # package at app/src/main/assets/game.dvxpak.
    set(_devex_android "${CMAKE_BINARY_DIR}/android")
    set(_devex_android_main "${_devex_android}/app/src/main")
    if(ANDROID_ABI STREQUAL "arm64-v8a")
        set(_devex_android_triple aarch64-linux-android)
    elseif(ANDROID_ABI STREQUAL "x86_64")
        set(_devex_android_triple x86_64-linux-android)
    else()
        message(FATAL_ERROR "Devex is not built for the Android ABI ${ANDROID_ABI}")
    endif()
    add_custom_command(TARGET devex_player POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/android" "${_devex_android}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/sdl-android/java"
                "${_devex_android_main}/java"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_devex_android_main}/jniLibs/${ANDROID_ABI}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:devex_player>" "$<TARGET_FILE:devex_engine>"
                "${CMAKE_SYSROOT}/usr/lib/${_devex_android_triple}/libc++_shared.so"
                "${_devex_android_main}/jniLibs/${ANDROID_ABI}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders"
                "${_devex_android_main}/assets/devex/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources"
                "${_devex_android_main}/assets/devex/resources"
        VERBATIM
    )
endif()
devex_set_warnings(devex_player)
devex_set_application_icon(devex_player)
# An exported game is this program renamed, with the engine library beside it: the game looks for
# the library there first, then in lib/ next to bin/, as in a build or an installed engine.
if(UNIX AND NOT APPLE)
    set_target_properties(devex_player PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib"
    )
endif()

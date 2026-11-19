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
    elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
        set(_devex_android_triple arm-linux-androideabi)
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
if(EMSCRIPTEN)
    # The game of a browser: devex-player.js and .wasm, and the page that loads the package beside them into the files
    # of the program before it starts. The memory grows with what the game loads.
    set(DEVEX_WEB_KIT_LINK_OPTIONS
        -sALLOW_MEMORY_GROWTH=1
        -sSTACK_SIZE=8MB
        -sFORCE_FILESYSTEM=1
        -lidbfs.js
        "-sEXPORTED_RUNTIME_METHODS=['FS','addRunDependency','removeRunDependency']"
    )
    # WebGL2, whose functions the renderer asks for by name, as from any OpenGL.
    if(DEVEX_OPENGL)
        list(APPEND DEVEX_WEB_KIT_LINK_OPTIONS -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sGL_ENABLE_GET_PROC_ADDRESS=1)
    endif()
    target_link_options(devex_player PRIVATE ${DEVEX_WEB_KIT_LINK_OPTIONS})
    # The player with threads goes beside the other in an export of Thread Support, under its own name, which its
    # program, its workers and its WebAssembly know each other by.
    set(DEVEX_WEB_KIT_COMPILE_OPTIONS -fwasm-exceptions)
    set(DEVEX_WEB_PLAYER_NAME "devex-player")
    if(DEVEX_WEB_THREADS)
        set(DEVEX_WEB_PLAYER_NAME "devex-player-threads")
        list(APPEND DEVEX_WEB_KIT_COMPILE_OPTIONS -pthread)
        list(APPEND DEVEX_WEB_KIT_LINK_OPTIONS -pthread "${DEVEX_WEB_THREAD_POOL}" -Wno-pthreads-mem-growth)
    endif()
    set_target_properties(devex_player PROPERTIES OUTPUT_NAME "${DEVEX_WEB_PLAYER_NAME}")
    add_custom_command(TARGET devex_player POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html"
                "$<TARGET_FILE_DIR:devex_player>/index.html"
        VERBATIM
    )

    # The kit with which an export for the web links the C++ of a game to the engine, in bin/kit: the link of this
    # player, written down as it is made, done again with the code of the game (DevexWebKit.cmake). A player that does
    # not draw with WebGPU has none.
    if(DEVEX_WEBGPU)
        set(_devex_link_record "${CMAKE_BINARY_DIR}/devex-player-link.cmake")
        set_target_properties(devex_player PROPERTIES CXX_LINKER_LAUNCHER
            "${CMAKE_COMMAND};-DRECORD=${_devex_link_record};-P;${PROJECT_SOURCE_DIR}/cmake/DevexWebKit.cmake;--")
        configure_file("${PROJECT_SOURCE_DIR}/cmake/DevexWebKitConfig.cmake.in"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/kit/cmake/DevexConfig.cmake" @ONLY)
        find_package(glm CONFIG REQUIRED)
        get_target_property(_devex_glm_include glm::glm-header-only INTERFACE_INCLUDE_DIRECTORIES)
        add_custom_target(devex_web_kit ALL
            COMMAND "${CMAKE_COMMAND}" "-DRECORD=${_devex_link_record}" "-DKIT=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/kit"
                    "-DSOURCE=${PROJECT_SOURCE_DIR}" "-DGLM=${_devex_glm_include}" "-DDAWN=${Dawn_DIR}"
                    -P "${PROJECT_SOURCE_DIR}/cmake/DevexWebKit.cmake"
            COMMENT "Putting together the web kit"
            VERBATIM
        )
        add_dependencies(devex_web_kit devex_player)
        set_target_properties(devex_web_kit PROPERTIES FOLDER "Applications")
    endif()
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

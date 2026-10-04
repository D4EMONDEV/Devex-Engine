# One neutral-language icon group shared by the editor and player. Game exports can replace
# group 1 with the project's icon through platform::setExecutableIcon.
if(WIN32)
    enable_language(RC)
    set(DEVEX_APPLICATION_ICON "${PROJECT_SOURCE_DIR}/engine/resources/icons/devex.ico")
    configure_file("${PROJECT_SOURCE_DIR}/engine/resources/devex.rc.in"
                   "${CMAKE_BINARY_DIR}/resources/devex.rc" @ONLY)
    add_library(devex_application_icon OBJECT "${CMAKE_BINARY_DIR}/resources/devex.rc")
    set_source_files_properties("${CMAKE_BINARY_DIR}/resources/devex.rc"
        PROPERTIES OBJECT_DEPENDS "${DEVEX_APPLICATION_ICON}")
    set_target_properties(devex_application_icon PROPERTIES FOLDER "Resources")
endif()

function(devex_set_application_icon target)
    if(WIN32)
        target_sources(${target} PRIVATE $<TARGET_OBJECTS:devex_application_icon>)
    endif()
endfunction()

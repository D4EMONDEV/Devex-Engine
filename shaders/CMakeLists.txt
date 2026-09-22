devex_add_shaders(devex_shaders
    OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders"
    SOURCES
        ibl.slang
        luminance.slang
        mesh.slang
        overlay.slang
        pick.slang
        shadow.slang
        sky.slang
        tonemap.slang
        ui.slang
    MODULES
        common.slang
        pbr.slang
)

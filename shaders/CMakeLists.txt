devex_add_shaders(devex_shaders
    OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders"
    SOURCES
        ao.slang
        bloom.slang
        ibl.slang
        luminance.slang
        mesh.slang
        overlay.slang
        particle.slang
        pick.slang
        prepass.slang
        shadow.slang
        sprite.slang
        sky.slang
        taa.slang
        tonemap.slang
        ui.slang
    MODULES
        canvas.slang
        common.slang
        custom.slang
        particles.slang
        pbr.slang
)

# The editor compiles the shaders of projects at their import, with slangc and these modules beside it.
devex_ship_shader_modules(devex_shader_modules
    OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/modules"
    MODULES
        canvas.slang
        common.slang
        custom.slang
        particles.slang
        pbr.slang
)
devex_ship_slangc(devex_slangc OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/slang")

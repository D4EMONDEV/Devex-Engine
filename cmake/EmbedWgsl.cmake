# Writes the WGSL that slangc made into a C++ file, for the WebGPU renderer, which has no files to load in a browser.
# cmake -DOUTPUT=<file.cpp> -DINPUTS=<a.wgsl;b.wgsl> -P EmbedWgsl.cmake

set(content "// Written by cmake/EmbedWgsl.cmake from the WGSL that slangc compiled: do not edit.\n")
string(APPEND content "#include \"WebGpuShaders.hpp\"\n\nnamespace devex::render::webgpu {\nnamespace {\n\n")
set(lookup "")
foreach(input IN LISTS INPUTS)
    cmake_path(GET input STEM name)
    file(READ "${input}" bytes HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${bytes}")
    # Lines of a reasonable length.
    string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){24})" "\\1\n    " bytes "${bytes}")
    string(APPEND content "constexpr unsigned char ${name}[] = {\n    ${bytes}0x00};\n\n")
    string(APPEND lookup "    if (name == \"${name}\")\n    {\n        return {reinterpret_cast<const char*>(${name}), sizeof(${name}) - 1};\n    }\n")
endforeach()
string(APPEND content "} // namespace\n\nstd::string_view shaderSource(std::string_view name) noexcept\n{\n${lookup}    return {};\n}\n\n")
string(APPEND content "} // namespace devex::render::webgpu\n")
file(WRITE "${OUTPUT}.new" "${content}")
file(COPY_FILE "${OUTPUT}.new" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.new")

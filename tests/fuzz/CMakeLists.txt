# Instrument the readers themselves, including their containers and field conversions.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "DEVEX_BUILD_FUZZERS requires Clang with libFuzzer on Linux.")
endif()

foreach(part IN ITEMS devex_core devex_reflection devex_serialization devex_asset devex_scene)
    target_compile_options(${part} PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all
        -fno-omit-frame-pointer
    )
endforeach()
# The engine is also loaded by tools such as bindgen in a fuzz build.
target_link_options(devex_engine PUBLIC -fsanitize=address,undefined)

foreach(reader IN ITEMS terrain scene mesh)
    add_executable(devex_fuzz_${reader} ${reader}.cpp)
    target_link_libraries(devex_fuzz_${reader} PRIVATE Devex::Engine)
    target_compile_options(devex_fuzz_${reader} PRIVATE
        -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all
        -fno-omit-frame-pointer
    )
    target_link_options(devex_fuzz_${reader} PRIVATE -fsanitize=fuzzer,address,undefined)
    set_target_properties(devex_fuzz_${reader} PROPERTIES FOLDER "Tests/Fuzz")
    devex_set_warnings(devex_fuzz_${reader})
endforeach()

add_executable(devex_fuzz_seed_corpus SeedCorpus.cpp)
target_link_libraries(devex_fuzz_seed_corpus PRIVATE Devex::Engine)
set_target_properties(devex_fuzz_seed_corpus PROPERTIES FOLDER "Tests/Fuzz")
devex_set_warnings(devex_fuzz_seed_corpus)

set(corpus_directory "${CMAKE_CURRENT_BINARY_DIR}/corpus")
add_custom_command(
    OUTPUT "${corpus_directory}/.seeded"
    COMMAND devex_fuzz_seed_corpus "${corpus_directory}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${corpus_directory}/.seeded"
    DEPENDS devex_fuzz_seed_corpus
    COMMENT "Seeding the fuzz corpora with real mesh, terrain and scene files"
    VERBATIM
)
add_custom_target(devex_fuzz_corpus DEPENDS "${corpus_directory}/.seeded")
add_custom_target(devex_fuzzers
    DEPENDS devex_fuzz_terrain devex_fuzz_scene devex_fuzz_mesh devex_fuzz_corpus)
set_target_properties(devex_fuzz_corpus devex_fuzzers PROPERTIES FOLDER "Tests/Fuzz")

# The game module of the project, built by the Devex editor whenever a file of this folder changes.
cmake_minimum_required(VERSION 3.25)
project(Game LANGUAGES CXX)

find_package(Devex CONFIG REQUIRED)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
devex_add_game_module(SOURCES ${sources})

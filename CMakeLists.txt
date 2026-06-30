cmake_minimum_required(VERSION 3.15)
project(ProSoccerDumper VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DLL olarak derleme ayarı
add_library(NovaDumper SHARED main.cpp)

set_target_properties(NovaDumper PROPERTIES
    LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    OUTPUT_NAME "NovaDumper"
)

target_compile_definitions(NovaDumper PRIVATE _USRDLL _WINDLL)

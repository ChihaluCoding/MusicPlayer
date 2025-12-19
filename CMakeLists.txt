cmake_minimum_required(VERSION 3.21)
project(MusicPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)

qt_standard_project_setup()

add_executable(MusicPlayer
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
)

target_link_libraries(MusicPlayer PRIVATE Qt6::Widgets Qt6::Multimedia)

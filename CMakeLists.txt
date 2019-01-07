cmake_minimum_required(VERSION 2.8)

project(pahkd)
add_executable(${PROJECT_NAME} pahkd.c)
set_property(TARGET ${PROJECT_NAME} PROPERTY C_STANDARD 99)
target_link_libraries(${PROJECT_NAME} xcb pulse)

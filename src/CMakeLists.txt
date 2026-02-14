# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(
    SRCS ${app_sources}
    EMBED_TXTFILES
        "${CMAKE_SOURCE_DIR}/data/ota_page.html"
        "${CMAKE_SOURCE_DIR}/data/pir312_page.html"
        "${CMAKE_SOURCE_DIR}/data/main_page.html"
        "${CMAKE_SOURCE_DIR}/data/style.css"
)

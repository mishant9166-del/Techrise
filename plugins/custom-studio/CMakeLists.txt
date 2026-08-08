project(custom-studio)

add_library(custom-studio MODULE custom-studio.c)

target_link_libraries(custom-studio PRIVATE libobs)

target_include_directories(custom-studio PRIVATE
    ${CMAKE_SOURCE_DIR}/libobs
    ${CMAKE_SOURCE_DIR}/libobs/util
)

set_target_properties(custom-studio PROPERTIES
    FOLDER "plugins"
)

install(TARGETS custom-studio
    RUNTIME DESTINATION "obs-plugins/64bit"
    LIBRARY DESTINATION "obs-plugins/64bit"
)

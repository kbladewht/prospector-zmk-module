set(PROSPECTOR_SHIELD_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(SCREEN_FONT_HEADER ${CMAKE_CURRENT_BINARY_DIR}/screen_font_montserrat.h)
set(MONTSERRAT_20_SOURCE ${APPLICATION_SOURCE_DIR}/components/vial-qmk/lib/lvgl/src/font/lv_font_montserrat_20.c)
set(MONTSERRAT_28_SOURCE ${APPLICATION_SOURCE_DIR}/components/vial-qmk/lib/lvgl/src/font/lv_font_montserrat_28.c)
add_custom_command(
    OUTPUT ${SCREEN_FONT_HEADER}
    COMMAND ${Python3_EXECUTABLE} ${PROSPECTOR_SHIELD_SRC}/generate_screen_font.py
            ${MONTSERRAT_20_SOURCE} ${MONTSERRAT_28_SOURCE} ${SCREEN_FONT_HEADER}
    DEPENDS ${PROSPECTOR_SHIELD_SRC}/generate_screen_font.py
            ${MONTSERRAT_20_SOURCE} ${MONTSERRAT_28_SOURCE}
    VERBATIM
)
add_custom_target(generate_screen_font DEPENDS ${SCREEN_FONT_HEADER})

target_sources(app PRIVATE
    ${PROSPECTOR_SHIELD_SRC}/backlight_init.c
    ${PROSPECTOR_SHIELD_SRC}/zephyr_status_screen.c
)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
add_dependencies(app generate_screen_font)


set(PROSPECTOR_SHIELD_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_sources(app PRIVATE
    ${PROSPECTOR_SHIELD_SRC}/backlight_init.c
    ${PROSPECTOR_SHIELD_SRC}/zephyr_status_screen.c
)


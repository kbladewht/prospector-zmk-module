# Prospector Scanner CMakeLists.txt
# Step-by-step restoration from display_test


# Prospector Scanner CMakeLists.txt
# Step-by-step restoration from display_test

    # Backlight initialization (always needed)
    target_sources(app PRIVATE ../src_across/backlight_init.c)

    # Crash recovery: turn fatal errors into a logged reboot instead of a
    # silent halt that looks like a display freeze
    target_sources(app PRIVATE ../src_across/fault_recovery.c)

    # CUSTOM screen support (from display_test)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE ../src_across/custom_status_screen.c)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE ../src_across/hello_widget.c)

    # Swipe gesture event (ZMK event system - thread-safe)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE ../src_across/events/swipe_gesture_event.c)

    # Touch handler (when touch is enabled)
    target_sources_ifdef(CONFIG_PROSPECTOR_TOUCH_ENABLED app PRIVATE ../src_across/touch_handler.c)

    # Keyboard state tracking lives in the module (src/scanner_core.c) so that
    # every scanner shield shares it - nothing shield-local to add here.

    # Display settings persistence (NVS)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE ../src_across/display_settings.c)

    # Brightness control with APDS9960 sensor for auto brightness
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE ../src_across/brightness_control.c)

    # System settings widget (Bootloader, Reset, Channel selector)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE ../src_across/system_settings_widget.c)

    # NerdFont for modifier icons
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE ../src_across/fonts/NerdFonts_Regular_40.c)

    # Prospector Display layouts (Carrefinho-inspired)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE ../src_across/prospector_layouts.c)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE ../src_across/field_layout.c)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE ../src_across/operator_layout.c)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE ../src_across/radii_layout.c)

    # Carrefinho custom fonts
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE
        ../src_across/fonts_carrefinho/DINish_Condensed_Semibold_20.c
        ../src_across/fonts_carrefinho/DINish_Condensed_Semibold_22.c
        ../src_across/fonts_carrefinho/DINish_Expanded_Light_36.c
        ../src_across/fonts_carrefinho/DINish_Medium_24.c
        ../src_across/fonts_carrefinho/FG_Medium_20.c
        ../src_across/fonts_carrefinho/FG_Medium_21.c
        ../src_across/fonts_carrefinho/FG_Medium_24.c
        ../src_across/fonts_carrefinho/FG_Medium_26.c
        ../src_across/fonts_carrefinho/FR_Medium_32.c
        ../src_across/fonts_carrefinho/FR_Regular_30.c
        ../src_across/fonts_carrefinho/FR_Regular_36.c
        ../src_across/fonts_carrefinho/FR_Regular_48.c
        ../src_across/fonts_carrefinho/FR_Thin_48.c
        ../src_across/fonts_carrefinho/PPF_NarrowThin_64.c
        ../src_across/fonts_carrefinho/Symbols_Bold_26.c
        ../src_across/fonts_carrefinho/Symbols_Medium_28.c
        ../src_across/fonts_carrefinho/Symbols_Regular_28.c
        ../src_across/fonts_carrefinho/Symbols_Semibold_28.c
        ../src_across/fonts_carrefinho/Symbols_Semibold_32.c
    )

    # Include path for local headers
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)


# ORIGINAL FULL BUILD - TO BE RESTORED INCREMENTALLY
# if(CONFIG_SHIELD_PROSPECTOR_SCANNER)
#   zephyr_library()
#   ... (original code preserved in comments)
# endif()

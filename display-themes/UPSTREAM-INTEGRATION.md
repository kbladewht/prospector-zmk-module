# Integration status on ogura-scanner-ms

This PR contributes the five-theme source directory, not a completed ms18 port.
The existing branch's CMake, Kconfig, board drivers and screen remain unchanged.
Do not add this directory to ZMK_EXTRA_MODULES on this branch expecting a working
ms18 display: its adapter requires the ms2 interfaces described in README.md.

Source baseline: S7venYoung/zmk-prospector, codex/kblade-dya-four-themes,
214409a; firmware source before extraction ad4ecff.

The target branch contains prospector_adapter/prospector_ms18, while the current
adapter requires boards/shields/src/{prospector_layouts.h,s7789_update.h,
fonts_carrefinho.h,display_settings.h} from the documented ms2 dependency.
The renderers use LVGL 9 APIs; this branch's display code/configuration also needs
version review before any native integration.

Remaining integration work:
- Provide a state adapter and the shared fonts for this branch's ms18 data flow.
- Verify or port LVGL APIs and 280x240 geometry against its actual display setup.
- Wire the optional theme manager to its screen lifecycle and settings.
- Build this branch and test on actual ms18 hardware.

Evidence: the module passes 120 real-LVGL host states against its documented ms2
baseline, with zero unsafe, clipped or overlapping rectangles. The pre-extraction
ad4ecff ms2 firmware built successfully. Neither proves an ms18 firmware build.
No Actions run or hardware flash is requested by this contribution.

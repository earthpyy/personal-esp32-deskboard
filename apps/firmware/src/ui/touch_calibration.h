#pragma once

// On-device 3-point resistive touch calibration.
//
// The XPT2046 driver maps raw ADC values linearly across the full 0-4095 range
// straight to pixels, which is inaccurate toward the screen edges (tabs on the
// bottom bar read short and land just above the target). This module captures
// three on-screen taps, computes the affine correction via the library's
// smartdisplay_compute_touch_calibration(), applies it to the global
// touch_calibration_data, and persists it in NVS so it survives reboots.

// Load a stored calibration from NVS and apply it. Returns true if a valid
// calibration was found and applied; false means the caller should calibrate.
bool touch_calibration_load();

// Show a brief on-screen "Press BOOT to calibrate" window while polling the
// BOOT button (GPIO0). Returns true if BOOT was pressed within the window.
bool touch_calibration_prompt();

// Run the interactive calibration flow (3 targets + 1 verification tap), then
// compute, apply, and persist the result. Blocks, pumping LVGL itself, until a
// verification tap passes. Restarts automatically if verification fails.
void touch_calibration_run();

#include "touch_calibration.h"

#include <algorithm>

#include <Arduino.h>
#include <Preferences.h>
#include <esp32_smartdisplay.h>
#include <lvgl.h>

// BOOT button on the CYD is wired to GPIO0. It cannot be held during reset
// (that strap-boots the ESP32 into USB download mode), so we poll it for a
// short window *after* boot instead.
static constexpr int kBootGpio = 0;
static constexpr uint32_t kPromptMs = 3000;

// A verification tap must land within this many pixels of the target for the
// calibration to be accepted.
static constexpr int32_t kVerifyTolerancePx = 20;

// NVS store. Bump kCalVersion if the stored layout ever changes.
static constexpr char kNvsNamespace[] = "touchcal";
static constexpr char kNvsKey[] = "cal";
static constexpr uint32_t kCalVersion = 1;

struct StoredCal
{
  uint32_t version;
  float alphaX, betaX, deltaX;
  float alphaY, betaY, deltaY;
};

static void apply_calibration(const StoredCal &s)
{
  touch_calibration_data.alphaX = s.alphaX;
  touch_calibration_data.betaX = s.betaX;
  touch_calibration_data.deltaX = s.deltaX;
  touch_calibration_data.alphaY = s.alphaY;
  touch_calibration_data.betaY = s.betaY;
  touch_calibration_data.deltaY = s.deltaY;
  touch_calibration_data.valid = true;
}

bool touch_calibration_load()
{
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/true))
    return false;

  StoredCal s;
  size_t n = prefs.getBytes(kNvsKey, &s, sizeof(s));
  prefs.end();

  if (n != sizeof(s) || s.version != kCalVersion)
    return false;

  apply_calibration(s);
  log_i("touch calibration loaded from NVS");
  return true;
}

static void save_calibration()
{
  StoredCal s;
  s.version = kCalVersion;
  s.alphaX = touch_calibration_data.alphaX;
  s.betaX = touch_calibration_data.betaX;
  s.deltaX = touch_calibration_data.deltaX;
  s.alphaY = touch_calibration_data.alphaY;
  s.betaY = touch_calibration_data.betaY;
  s.deltaY = touch_calibration_data.deltaY;

  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/false))
  {
    log_w("could not open NVS to save touch calibration");
    return;
  }
  prefs.putBytes(kNvsKey, &s, sizeof(s));
  prefs.end();
  log_i("touch calibration saved to NVS");
}

// Pump LVGL (tick + timers) for `ms`, ignoring input. Used for on-screen
// feedback and to debounce between taps.
static void pump(uint32_t ms)
{
  uint32_t start = millis();
  uint32_t last = start;
  while (millis() - start < ms)
  {
    uint32_t now = millis();
    lv_tick_inc(now - last);
    last = now;
    lv_timer_handler();
    delay(5);
  }
}

// Block until one complete tap (press then release) occurs. Samples the point
// throughout the press and returns the median of x and y. Resistive readings
// are noisy at initial contact and while pressure drops during release, so the
// first and last samples are dropped and the median rejects the rest of the
// jitter — far steadier than any single reading.
static void wait_for_tap(lv_indev_t *indev, lv_point_t *out)
{
  constexpr int kMaxSamples = 64;
  int32_t xs[kMaxSamples];
  int32_t ys[kMaxSamples];
  int count = 0;
  bool pressing = false;
  uint32_t last = millis();
  while (true)
  {
    uint32_t now = millis();
    lv_tick_inc(now - last);
    last = now;
    lv_timer_handler();

    bool pressed = lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;
    if (pressed)
    {
      lv_point_t p;
      lv_indev_get_point(indev, &p);
      if (count < kMaxSamples)
      {
        xs[count] = p.x;
        ys[count] = p.y;
        count++;
      }
      pressing = true;
    }
    else if (pressing && count > 0)
    {
      // Drop the contact and release samples when we have enough to spare.
      int lo = 0;
      int hi = count;
      if (count >= 5)
      {
        lo = 1;
        hi = count - 1;
      }
      std::sort(xs + lo, xs + hi);
      std::sort(ys + lo, ys + hi);
      int mid = lo + (hi - lo) / 2;
      out->x = xs[mid];
      out->y = ys[mid];
      return;
    }
    delay(5);
  }
}

// Draw a crosshair (ring + centre dot) with its centre exactly at (x, y).
static lv_obj_t *make_crosshair(lv_obj_t *parent, int32_t x, int32_t y, lv_color_t color)
{
  lv_obj_t *ring = lv_obj_create(parent);
  lv_obj_remove_style_all(ring);
  lv_obj_set_size(ring, 28, 28);
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(ring, 2, 0);
  lv_obj_set_style_border_color(ring, color, 0);
  lv_obj_set_pos(ring, x - 14, y - 14);

  lv_obj_t *dot = lv_obj_create(ring);
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, 6, 6);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, color, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_center(dot);
  return ring;
}

bool touch_calibration_prompt()
{
  pinMode(kBootGpio, INPUT_PULLUP);

  lv_obj_t *scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

  lv_obj_t *label = lv_label_create(scr);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(label);

  bool pressed = false;
  uint32_t start = millis();
  uint32_t last = start;
  int32_t shown_secs = -1;
  while (millis() - start < kPromptMs)
  {
    uint32_t now = millis();
    lv_tick_inc(now - last);
    last = now;

    int32_t secs = (kPromptMs - (now - start) + 999) / 1000;
    if (secs != shown_secs)
    {
      lv_label_set_text_fmt(label, "Press BOOT button\nto calibrate touch\n\n%ld", (long)secs);
      shown_secs = secs;
    }

    lv_timer_handler();
    if (digitalRead(kBootGpio) == LOW)
    {
      pressed = true;
      break;
    }
    delay(5);
  }

  lv_obj_clean(scr);
  return pressed;
}

void touch_calibration_run()
{
  lv_indev_t *indev = lv_indev_get_next(nullptr);

  auto disp = lv_display_get_default();
  int32_t w = lv_display_get_horizontal_resolution(disp);
  int32_t h = lv_display_get_vertical_resolution(disp);

  // Three well-spread, non-collinear targets inset from the edges; the third
  // sits low, near where the bottom tab bar lives.
  lv_point_t screen[3];
  screen[0].x = w * 15 / 100;
  screen[0].y = h * 15 / 100;
  screen[1].x = w * 85 / 100;
  screen[1].y = h * 15 / 100;
  screen[2].x = w * 50 / 100;
  screen[2].y = h * 85 / 100;
  lv_point_t verify = {w / 2, h / 2};

  lv_obj_t *scr = lv_screen_active();

  bool done = false;
  while (!done)
  {
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_obj_t *root = lv_obj_create(scr);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, w, h);
    lv_obj_set_pos(root, 0, 0);

    lv_obj_t *label = lv_label_create(root);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    // Read raw sensor coordinates while capturing the calibration points.
    touch_calibration_data.valid = false;
    lv_point_t touch[3];
    for (int i = 0; i < 3; i++)
    {
      lv_label_set_text_fmt(label, "Tap the target\n%d of 3", i + 1);
      lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
      lv_obj_t *cross = make_crosshair(root, screen[i].x, screen[i].y, lv_color_white());

      wait_for_tap(indev, &touch[i]);
      log_i("cal point %d: screen(%ld,%ld) touch(%ld,%ld)", i + 1,
            (long)screen[i].x, (long)screen[i].y, (long)touch[i].x, (long)touch[i].y);

      // green flash as capture feedback + debounce before the next target
      lv_obj_set_style_border_color(cross, lv_color_hex(0x00c853), 0);
      pump(350);
      lv_obj_delete(cross);
    }

    touch_calibration_data = smartdisplay_compute_touch_calibration(screen, touch);

    // Verification tap now runs through the fresh calibration.
    lv_label_set_text(label, "Tap the target\nto confirm");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 12);
    make_crosshair(root, verify.x, verify.y, lv_color_hex(0x00c853));

    lv_point_t v;
    wait_for_tap(indev, &v);
    int32_t dx = v.x - verify.x;
    int32_t dy = v.y - verify.y;
    bool ok = (dx * dx + dy * dy) <= (kVerifyTolerancePx * kVerifyTolerancePx);
    log_i("verify: target(%ld,%ld) tap(%ld,%ld) %s", (long)verify.x, (long)verify.y,
          (long)v.x, (long)v.y, ok ? "OK" : "FAIL");

    if (ok)
    {
      save_calibration();
      lv_label_set_text(label, "Calibrated");
      lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
      pump(800);
      done = true;
    }
    else
    {
      touch_calibration_data.valid = false;
      lv_label_set_text(label, "Off target\nlet's try again");
      lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
      pump(1200);
    }
  }

  lv_obj_clean(scr);
}

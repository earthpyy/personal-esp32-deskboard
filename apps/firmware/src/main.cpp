#include <Arduino.h>
#include <esp32_smartdisplay.h>

static lv_obj_t *counter_label;
static uint32_t press_count = 0;

static void btn_event_cb(lv_event_t *e)
{
  press_count++;
  lv_label_set_text_fmt(counter_label, "Pressed: %lu", (unsigned long)press_count);
  log_i("Button pressed %lu times", (unsigned long)press_count);
}

void setup()
{
  Serial.begin(115200);
  smartdisplay_init();
  smartdisplay_lcd_set_backlight(0.75f);

  auto screen = lv_screen_active();

  auto title = lv_label_create(screen);
  lv_label_set_text(title, "ESP32 Deskboard");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

  auto btn = lv_button_create(screen);
  lv_obj_set_size(btn, 140, 56);
  lv_obj_center(btn);
  lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, nullptr);

  auto btn_label = lv_label_create(btn);
  lv_label_set_text(btn_label, "Tap me");
  lv_obj_center(btn_label);

  counter_label = lv_label_create(screen);
  lv_label_set_text(counter_label, "Pressed: 0");
  lv_obj_align(counter_label, LV_ALIGN_BOTTOM_MID, 0, -24);
}

static auto lv_last_tick = millis();

void loop()
{
  auto const now = millis();
  lv_tick_inc(now - lv_last_tick);
  lv_last_tick = now;
  lv_timer_handler();
}

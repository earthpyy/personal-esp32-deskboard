#include <Arduino.h>
#include <esp32_smartdisplay.h>

#include "hw_stats.h"
#include "net/net.h"
#include "net/schedule_client.h"
#include "ui/ui.h"

void setup()
{
  Serial.begin(115200);
  smartdisplay_init();
  lv_display_set_rotation(lv_display_get_default(), LV_DISPLAY_ROTATION_270);
  smartdisplay_lcd_set_backlight(0.75f);

  hw_stats_init();
  net_init();
  schedule_client_init();
  ui_init();
}

static auto lv_last_tick = millis();

void loop()
{
  auto const now = millis();
  lv_tick_inc(now - lv_last_tick);
  lv_last_tick = now;
  auto const wait_ms = lv_timer_handler();
  // sleep until LVGL needs to run again so the idle task gets time on this
  // core (required for the CPU % estimate); cap to keep input responsive
  delay(wait_ms == LV_NO_TIMER_READY ? 5 : (wait_ms > 10 ? 10 : wait_ms));
}

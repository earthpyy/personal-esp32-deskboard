#include "page_monitor.h"

#include "../hw_stats.h"

#include <Arduino.h>

static lv_obj_t *cpu0_value;
static lv_obj_t *cpu1_value;
static lv_obj_t *fps_value;
static lv_obj_t *render_value;
static lv_obj_t *heap_value;
static lv_obj_t *heap_min_value;
static lv_obj_t *lv_mem_value;
static lv_obj_t *uptime_value;
static lv_timer_t *refresh_timer;

// FPS = LVGL display refresh cycles per second; render ms = average cycle
// duration. Measured here (not in hw_stats) to keep hw_stats LVGL-free.
static volatile uint32_t refr_count = 0;
static volatile uint32_t render_ms_total = 0;
static uint32_t refr_start_ms = 0;
static uint32_t last_fps_sample_ms = 0;

static void refr_start_cb(lv_event_t *e)
{
  refr_start_ms = millis();
}

static void refr_ready_cb(lv_event_t *e)
{
  refr_count++;
  render_ms_total += millis() - refr_start_ms;
}

static lv_obj_t *add_row(lv_obj_t *parent, const char *name)
{
  auto row = lv_obj_create(parent);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(row, 6, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  auto name_label = lv_label_create(row);
  lv_label_set_text(name_label, name);

  auto value_label = lv_label_create(row);
  lv_label_set_text(value_label, "--");
  return value_label;
}

static void set_pct_text(lv_obj_t *label, float pct)
{
  if (pct < 0.0f)
    lv_label_set_text(label, "--");
  else
    lv_label_set_text_fmt(label, "%d%%", (int)(pct + 0.5f));
}

static void refresh_cb(lv_timer_t *timer)
{
  HwStats stats;
  hw_stats_sample(&stats);

  set_pct_text(cpu0_value, stats.cpu_pct[0]);
  if (cpu1_value != nullptr)
    set_pct_text(cpu1_value, stats.cpu_pct[1]);

  auto const now = millis();
  auto const dt_ms = now - last_fps_sample_ms;
  last_fps_sample_ms = now;
  uint32_t const frames = refr_count;
  uint32_t const render_total = render_ms_total;
  refr_count = 0;
  render_ms_total = 0;
  if (dt_ms > 0 && frames > 0)
  {
    lv_label_set_text_fmt(fps_value, "%lu", (unsigned long)(frames * 1000 / dt_ms));
    lv_label_set_text_fmt(render_value, "%lu ms", (unsigned long)(render_total / frames));
  }
  else
  {
    lv_label_set_text(fps_value, "--");
    lv_label_set_text(render_value, "--");
  }

  lv_label_set_text_fmt(heap_value, "%lu / %lu KB", (unsigned long)(stats.heap_free / 1024), (unsigned long)(stats.heap_total / 1024));
  lv_label_set_text_fmt(heap_min_value, "%lu KB", (unsigned long)(stats.heap_min_free / 1024));

  lv_mem_monitor_t mem;
  lv_mem_monitor(&mem);
  lv_label_set_text_fmt(lv_mem_value, "%d%%", mem.used_pct);

  lv_label_set_text_fmt(uptime_value, "%02lu:%02lu:%02lu",
                        (unsigned long)(stats.uptime_s / 3600),
                        (unsigned long)(stats.uptime_s / 60 % 60),
                        (unsigned long)(stats.uptime_s % 60));
}

void page_monitor_build(lv_obj_t *parent)
{
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(parent, 4, 0);
  lv_obj_set_style_pad_row(parent, 0, 0);

  cpu0_value = add_row(parent, "CPU 0");
  if (ESP.getChipCores() > 1)
    cpu1_value = add_row(parent, "CPU 1");
  fps_value = add_row(parent, "FPS");
  render_value = add_row(parent, "Render");
  heap_value = add_row(parent, "Heap free");
  heap_min_value = add_row(parent, "Heap min");
  lv_mem_value = add_row(parent, "LVGL mem");
  uptime_value = add_row(parent, "Uptime");

  ChipInfo chip;
  hw_stats_chip_info(&chip);
  auto chip_label = lv_label_create(parent);
  lv_obj_set_width(chip_label, lv_pct(100));
  lv_label_set_long_mode(chip_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_label_set_text_fmt(chip_label, "%s rev%u  %lu MHz  %lu MB flash",
                        chip.model, chip.revision,
                        (unsigned long)chip.cpu_mhz,
                        (unsigned long)(chip.flash_size / (1024 * 1024)));
  lv_obj_set_style_pad_all(chip_label, 6, 0);

  auto display = lv_display_get_default();
  lv_display_add_event_cb(display, refr_start_cb, LV_EVENT_REFR_START, nullptr);
  lv_display_add_event_cb(display, refr_ready_cb, LV_EVENT_REFR_READY, nullptr);
  last_fps_sample_ms = millis();

  refresh_timer = lv_timer_create(refresh_cb, 1000, nullptr);
  lv_timer_pause(refresh_timer);
}

void page_monitor_set_active(bool active)
{
  if (refresh_timer == nullptr)
    return;
  if (active)
  {
    lv_timer_resume(refresh_timer);
    lv_timer_ready(refresh_timer); // refresh immediately on entry
  }
  else
  {
    lv_timer_pause(refresh_timer);
  }
}

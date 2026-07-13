#include "page_schedule.h"

#include "../net/net.h"
#include "../net/schedule_client.h"

#include <Arduino.h>
#include <secrets.h>

static ScheduleData *data; // heap-allocated in build (dram0 .bss is full)
static bool has_data = false;

static lv_obj_t *date_label;
static lv_obj_t *stale_btn; // warning icon + "as of HH:MM"
static lv_obj_t *allday_row;
static lv_obj_t *list;
static lv_obj_t *now_line;
static lv_obj_t *debug_panel;
static lv_obj_t *debug_label;
static lv_obj_t *empty_label;
static lv_obj_t *cards[SCHEDULE_MAX_EVENTS];
static lv_timer_t *tick_timer;
static int last_focus = -1;
static bool debug_forced = false;
static uint32_t tick_count = 0;

static constexpr uint32_t STALE_AFTER_MS = 3 * 60 * 1000;

static void set_hidden(lv_obj_t *obj, bool hidden)
{
  if (hidden)
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

// index of the current event, else the next upcoming, else the last one; -1 if none
static int focus_index(int64_t now)
{
  for (int i = 0; i < data->event_count; i++)
    if (data->events[i].end > now)
      return i;
  return data->event_count > 0 ? data->event_count - 1 : -1;
}

static void restyle(bool force_scroll)
{
  if (!has_data)
    return;
  auto const now = schedule_client_now(data);
  int past_count = 0;
  for (int i = 0; i < data->event_count; i++)
  {
    auto const &ev = data->events[i];
    auto const past = ev.end <= now;
    auto const current = ev.start <= now && now < ev.end;
    if (past)
      past_count++;
    lv_obj_set_style_opa(cards[i], past ? LV_OPA_50 : LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cards[i], current ? 2 : 0, 0);
  }
  // now-line sits after the last past card (cards precede it after rebuild)
  lv_obj_move_to_index(now_line, past_count);

  auto const focus = focus_index(now);
  if (focus >= 0 && (force_scroll || focus != last_focus))
    lv_obj_scroll_to_view(cards[focus], LV_ANIM_ON);
  last_focus = focus;
}

static void rebuild()
{
  lv_obj_clean(allday_row);
  for (int i = 0; i < data->all_day_count; i++)
  {
    auto const &ad = data->all_day[i];
    auto chip = lv_obj_create(allday_row);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(chip, 8, 0);
    lv_obj_set_style_pad_ver(chip, 3, 0);
    lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_bg_color(chip, lv_color_hex(ad.color), 0);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    auto label = lv_label_create(chip);
    lv_label_set_text(label, ad.title);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(label, 140, 0);
  }
  set_hidden(allday_row, data->all_day_count == 0 || debug_forced);

  // wipe cards but keep the now-line object: re-parent it out, clean, re-add
  lv_obj_set_parent(now_line, lv_obj_get_parent(list));
  lv_obj_clean(list);
  lv_obj_set_parent(now_line, list);

  for (int i = 0; i < data->event_count; i++)
  {
    auto const &ev = data->events[i];
    auto card = lv_obj_create(list);
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_left(card, 14, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_border_color(card, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 2, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    auto bar = lv_obj_create(card);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(bar, 4, lv_pct(100));
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, -10, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(ev.color), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 2, 0);

    auto title = lv_label_create(card);
    lv_label_set_text(title, ev.title);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, lv_pct(100));

    auto sub = lv_label_create(card);
    lv_label_set_text_fmt(sub, "%s  -  %s", ev.time_label, ev.calendar);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_opa(sub, LV_OPA_60, 0);

    cards[i] = card;
  }
  // move the line to the front; restyle() will place it correctly
  lv_obj_move_to_index(now_line, 0);
  set_hidden(now_line, data->event_count == 0);

  empty_label = lv_label_create(list);
  lv_label_set_text(empty_label, "No events today");
  lv_obj_set_style_text_opa(empty_label, LV_OPA_60, 0);
  set_hidden(empty_label, data->event_count > 0 || data->all_day_count > 0 || debug_forced);

  lv_label_set_text(date_label, data->date_label);
  last_focus = -1; // force scroll re-evaluation
}

static void update_debug()
{
  auto const show = debug_forced || !has_data;
  set_hidden(debug_panel, !show);
  set_hidden(list, show);
  if (!show)
    return;
  lv_label_set_text_fmt(debug_label,
                        "Server: %s\n"
                        "WiFi: %s (%s)\n"
                        "IP: %s\n"
                        "RSSI: %d dBm\n"
                        "Last fetch OK: %s",
                        API_BASE_URL,
                        net_status_str(), net_ssid(),
                        net_connected() ? net_ip_str() : "--",
                        net_connected() ? net_rssi() : 0,
                        schedule_client_last_success_ms() > 0 ? "yes" : "never");
}

static void update_stale()
{
  auto const last_ok = schedule_client_last_success_ms();
  auto const stale = last_ok > 0 && millis() - last_ok > STALE_AFTER_MS;
  set_hidden(stale_btn, !stale);
  if (stale)
  {
    auto label = lv_obj_get_child(stale_btn, 0);
    lv_label_set_text_fmt(label, LV_SYMBOL_WARNING " as of %s", data->now_label);
  }
  else if (debug_forced && has_data)
  {
    debug_forced = false; // auto-close the debug panel once healthy again
  }
}

static void stale_clicked_cb(lv_event_t *e)
{
  debug_forced = !debug_forced;
  update_debug();
}

static void tick_cb(lv_timer_t *timer)
{
  if (schedule_client_take_fresh(data))
  {
    has_data = true;
    rebuild();
    restyle(true);
  }
  else if (has_data && tick_count % 30 == 0)
  {
    restyle(false);
  }
  update_stale();
  update_debug();
  tick_count++;
}

void page_schedule_build(lv_obj_t *parent)
{
  data = new ScheduleData();

  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(parent, 4, 0);
  lv_obj_set_style_pad_row(parent, 4, 0);
  lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  auto header = lv_obj_create(parent);
  lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(header, 4, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  date_label = lv_label_create(header);
  lv_label_set_text(date_label, "Schedule");

  stale_btn = lv_obj_create(header);
  lv_obj_set_size(stale_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(stale_btn, 2, 0);
  lv_obj_set_style_border_width(stale_btn, 0, 0);
  lv_obj_add_flag(stale_btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(stale_btn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(stale_btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(stale_btn, stale_clicked_cb, LV_EVENT_CLICKED, nullptr);
  auto stale_label = lv_label_create(stale_btn);
  lv_obj_set_style_text_font(stale_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(stale_label, lv_palette_main(LV_PALETTE_ORANGE), 0);
  lv_label_set_text(stale_label, LV_SYMBOL_WARNING);

  allday_row = lv_obj_create(parent);
  lv_obj_set_size(allday_row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(allday_row, 2, 0);
  lv_obj_set_style_pad_column(allday_row, 4, 0);
  lv_obj_set_style_border_width(allday_row, 0, 0);
  lv_obj_set_flex_flow(allday_row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_remove_flag(allday_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(allday_row, LV_OBJ_FLAG_HIDDEN);

  list = lv_obj_create(parent);
  lv_obj_set_width(list, lv_pct(100));
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_style_pad_all(list, 2, 0);
  lv_obj_set_style_pad_row(list, 4, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

  now_line = lv_obj_create(list);
  lv_obj_set_size(now_line, lv_pct(100), 2);
  lv_obj_set_style_bg_color(now_line, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_set_style_border_width(now_line, 0, 0);
  lv_obj_set_style_radius(now_line, 1, 0);
  lv_obj_add_flag(now_line, LV_OBJ_FLAG_HIDDEN);

  empty_label = lv_label_create(list);
  lv_label_set_text(empty_label, "No events today");
  lv_obj_set_style_text_opa(empty_label, LV_OPA_60, 0);
  lv_obj_add_flag(empty_label, LV_OBJ_FLAG_HIDDEN);

  debug_panel = lv_obj_create(parent);
  lv_obj_set_width(debug_panel, lv_pct(100));
  lv_obj_set_flex_grow(debug_panel, 1);
  lv_obj_set_style_pad_all(debug_panel, 8, 0);
  lv_obj_set_style_border_width(debug_panel, 0, 0);
  lv_obj_add_flag(debug_panel, LV_OBJ_FLAG_HIDDEN);
  debug_label = lv_label_create(debug_panel);
  lv_obj_set_style_text_font(debug_label, &lv_font_montserrat_12, 0);
  lv_label_set_text(debug_label, "Connecting...");

  tick_timer = lv_timer_create(tick_cb, 1000, nullptr);
  lv_timer_pause(tick_timer);
}

void page_schedule_set_active(bool active)
{
  if (tick_timer == nullptr)
    return;
  if (active)
  {
    lv_timer_resume(tick_timer);
    lv_timer_ready(tick_timer);
    if (has_data)
      restyle(true); // re-snap to current/next on tab entry
  }
  else
  {
    lv_timer_pause(tick_timer);
  }
}

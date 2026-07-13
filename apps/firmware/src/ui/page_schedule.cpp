#include "page_schedule.h"

#include "../net/net.h"
#include "../net/schedule_client.h"

#include <Arduino.h>
#include <esp_random.h>
#include <secrets.h>
#include <string.h>

// one-glyph fork/knife icon for the lunch divider (src/ui/font_lunch.c)
LV_FONT_DECLARE(font_lunch);
#define LUNCH_GLYPH "\xEF\x83\xB5" // fa-cutlery U+F0F5

static ScheduleData *data; // heap-allocated in build (dram0 .bss is full)
static bool has_data = false;

static lv_obj_t *date_label;
static lv_obj_t *clock_label; // live "HH:MM" wall clock, top-right
static lv_obj_t *stale_btn; // warning icon + "as of HH:MM"
static lv_obj_t *allday_row;
static lv_obj_t *list;
static lv_obj_t *now_line;
static lv_obj_t *nextin_label; // "next in Xh Ym" at the right end of the now-line
static lv_obj_t *lunch_line; // "lunch break" divider at the free lunch slot
static lv_obj_t *debug_panel;
static lv_obj_t *debug_label;
static lv_obj_t *empty_label;
static lv_obj_t *done_label; // end-of-day message once every event has ended
static lv_obj_t *cards[SCHEDULE_MAX_EVENTS];
static lv_timer_t *tick_timer;
static int last_focus = -1;
static int done_phrase = -1; // index into DONE_PHRASES; -1 = message hidden
static bool debug_forced = false;
static uint32_t tick_count = 0;

static constexpr uint32_t STALE_AFTER_MS = 3 * 60 * 1000;

// Shown at the bottom of the list when the whole day is done. One is picked at
// random when the message appears and kept stable until it hides / a new day.
static const char *DONE_PHRASES[] = {
    "That's it for today!",
    "Enjoy the rest of your day!",
    "All done for today!",
    "You're all caught up!",
    "Nothing left on the books!",
    "That's a wrap!",
    "The rest of the day is yours!",
    "Nice work today!",
    "Time to unwind!",
    "No more meetings ahead!",
};
static constexpr int DONE_PHRASE_COUNT = sizeof(DONE_PHRASES) / sizeof(DONE_PHRASES[0]);

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
  // Place the time markers among the cards. Every non-card child sits at the
  // tail, so an index <= event_count counts only cards before it. Park the
  // markers first to normalize their start positions, then move them in
  // ascending index order so an earlier insert isn't shifted by a later one.
  bool const lunch_on = data->lunch_show && data->event_count > 0;
  int lunch_cards = 0; // cards that start before the lunch window
  if (lunch_on)
    for (int i = 0; i < data->event_count; i++)
      if (data->events[i].start < data->lunch_start)
        lunch_cards++;

  // when the now-line and lunch divider fall on the same card boundary, order
  // them by clock time so the divider reads correctly relative to "now"
  bool const lunch_first = lunch_on
      && (lunch_cards < past_count
          || (lunch_cards == past_count && data->lunch_start <= now));
  int const now_idx = past_count + (lunch_first ? 1 : 0);
  int const lunch_idx = lunch_cards + (lunch_on && !lunch_first ? 1 : 0);

  auto const park = [](lv_obj_t *o) {
    lv_obj_move_to_index(o, lv_obj_get_child_count(list) - 1);
  };
  park(now_line);
  park(lunch_line);
  park(done_label);

  if (lunch_on && lunch_first)
  {
    lv_obj_move_to_index(lunch_line, lunch_idx);
    lv_obj_move_to_index(now_line, now_idx);
  }
  else if (lunch_on)
  {
    lv_obj_move_to_index(now_line, now_idx);
    lv_obj_move_to_index(lunch_line, lunch_idx);
  }
  else
  {
    lv_obj_move_to_index(now_line, now_idx);
  }
  set_hidden(lunch_line, !lunch_on);

  // every event has ended once nothing sits past the now-line
  auto const all_past = data->event_count > 0 && past_count == data->event_count;
  auto just_appeared = false;
  if (all_past)
  {
    if (done_phrase < 0)
    {
      done_phrase = (int)(esp_random() % DONE_PHRASE_COUNT);
      lv_label_set_text(done_label, DONE_PHRASES[done_phrase]);
      just_appeared = true;
    }
    lv_obj_move_to_index(done_label, now_idx + 1); // right below the now-line
  }
  else
  {
    done_phrase = -1; // reset so a fresh phrase is drawn next time
  }
  set_hidden(done_label, !all_past);

  auto const focus = focus_index(now);
  if (all_past)
  {
    if (force_scroll || just_appeared)
      lv_obj_scroll_to_view(done_label, LV_ANIM_ON);
  }
  else if (focus >= 0 && (force_scroll || focus != last_focus))
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
    lv_obj_set_style_text_font(label, &noto_thai_12, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_width(label, 280, 0);
  }
  set_hidden(allday_row, data->all_day_count == 0 || debug_forced);

  // wipe cards but keep the now-line and done-message objects (they hold state):
  // re-parent them out, clean, then re-add *after* the cards below
  auto const list_parent = lv_obj_get_parent(list);
  lv_obj_set_parent(now_line, list_parent);
  lv_obj_set_parent(lunch_line, list_parent);
  lv_obj_set_parent(done_label, list_parent);
  lv_obj_clean(list);

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
    lv_obj_set_style_text_font(sub, &noto_thai_12, 0);
    lv_obj_set_style_text_opa(sub, LV_OPA_60, 0);

    cards[i] = card;
  }

  // Bring the stateful objects back in *after* the cards so the cards occupy the
  // leading child indices 0..N-1. restyle() positions the now-line purely by
  // past-card count, so anything ahead of the cards would offset it.
  lv_obj_set_parent(now_line, list);
  lv_obj_set_parent(lunch_line, list);
  lv_obj_set_parent(done_label, list);

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
  set_hidden(clock_label, stale || !has_data); // stale warning takes over the corner
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

// Live "HH:MM" clock (top-right) plus the "next in Xh Ym" countdown in the
// now-line. Both are minute-resolution; cached strings keep the labels from
// redrawing every second. Clock visibility is owned by update_stale().
static void update_clock_and_nextin()
{
  static char clock_str[8] = "";
  static char nextin_str[24] = "";

  if (!has_data)
    return;

  // wall clock: server's "HH:MM" at fetch + seconds elapsed since, wrapped daily
  auto const *lbl = data->now_label;
  int const base = ((lbl[0] - '0') * 10 + (lbl[1] - '0')) * 3600 +
                   ((lbl[3] - '0') * 10 + (lbl[4] - '0')) * 60;
  int const elapsed = (int)((millis() - data->received_ms) / 1000);
  int const sod = ((base + elapsed) % 86400 + 86400) % 86400;
  char clock_buf[8];
  lv_snprintf(clock_buf, sizeof(clock_buf), "%02d:%02d", sod / 3600, (sod % 3600) / 60);
  if (strcmp(clock_buf, clock_str) != 0)
  {
    strcpy(clock_str, clock_buf);
    lv_label_set_text(clock_label, clock_str);
  }

  // "next in": countdown to the next not-yet-started event; hidden while a
  // meeting is in progress and when nothing upcoming remains
  auto const now = schedule_client_now(data);
  bool in_meeting = false;
  int64_t next_start = -1;
  for (int i = 0; i < data->event_count; i++)
  {
    auto const &ev = data->events[i];
    if (ev.start <= now && now < ev.end)
      in_meeting = true;
    if (ev.start > now && next_start < 0)
      next_start = ev.start;
  }
  bool const show = next_start >= 0 && !in_meeting;
  set_hidden(nextin_label, !show);
  if (show)
  {
    int64_t const mins = (next_start - now + 59) / 60; // ceil to the minute
    char nextin_buf[24];
    if (mins >= 60)
      lv_snprintf(nextin_buf, sizeof(nextin_buf), "next in %lldh %lldm",
                  (long long)(mins / 60), (long long)(mins % 60));
    else
      lv_snprintf(nextin_buf, sizeof(nextin_buf), "next in %lldm", (long long)mins);
    if (strcmp(nextin_buf, nextin_str) != 0)
    {
      strcpy(nextin_str, nextin_buf);
      lv_label_set_text(nextin_label, nextin_str);
    }
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
  update_clock_and_nextin();
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
  lv_obj_set_style_text_font(stale_label, &noto_thai_12, 0);
  lv_obj_set_style_text_color(stale_label, lv_palette_main(LV_PALETTE_ORANGE), 0);
  lv_label_set_text(stale_label, LV_SYMBOL_WARNING);

  clock_label = lv_label_create(header);
  lv_label_set_text(clock_label, "");
  lv_obj_add_flag(clock_label, LV_OBJ_FLAG_HIDDEN); // shown once data arrives

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

  // now-line: a "now" pill on the left with a thin red rule filling the rest
  now_line = lv_obj_create(list);
  lv_obj_set_width(now_line, lv_pct(100));
  lv_obj_set_height(now_line, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(now_line, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(now_line, 0, 0);
  lv_obj_set_style_pad_all(now_line, 0, 0);
  lv_obj_set_flex_flow(now_line, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(now_line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(now_line, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(now_line, LV_OBJ_FLAG_HIDDEN);

  auto now_badge = lv_label_create(now_line);
  lv_label_set_text(now_badge, "now");
  lv_obj_set_style_text_font(now_badge, &noto_thai_12, 0);
  lv_obj_set_style_text_color(now_badge, lv_color_white(), 0);
  lv_obj_set_style_bg_color(now_badge, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_set_style_bg_opa(now_badge, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(now_badge, 6, 0);
  lv_obj_set_style_pad_ver(now_badge, 1, 0);
  lv_obj_set_style_radius(now_badge, LV_RADIUS_CIRCLE, 0);

  // "next in Xh Ym": sits just right of the "now" badge; the rule below then
  // flex-grows to fill the remaining width out to the edge
  nextin_label = lv_label_create(now_line);
  lv_label_set_text(nextin_label, "");
  lv_obj_set_style_text_font(nextin_label, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(nextin_label, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_set_style_margin_left(nextin_label, 6, 0);
  lv_obj_add_flag(nextin_label, LV_OBJ_FLAG_HIDDEN);

  auto now_rule = lv_obj_create(now_line);
  lv_obj_set_height(now_rule, 2);
  lv_obj_set_flex_grow(now_rule, 1);
  lv_obj_set_style_bg_color(now_rule, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_set_style_bg_opa(now_rule, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(now_rule, 0, 0);
  lv_obj_set_style_radius(now_rule, 1, 0);
  lv_obj_set_style_pad_all(now_rule, 0, 0);
  lv_obj_set_style_margin_left(now_rule, 4, 0);
  lv_obj_remove_flag(now_rule, LV_OBJ_FLAG_SCROLLABLE);

  empty_label = lv_label_create(list);
  lv_label_set_text(empty_label, "No events today");
  lv_obj_set_style_text_opa(empty_label, LV_OPA_60, 0);
  lv_obj_add_flag(empty_label, LV_OBJ_FLAG_HIDDEN);

  done_label = lv_label_create(list);
  lv_obj_set_width(done_label, lv_pct(100));
  lv_obj_set_style_text_align(done_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_opa(done_label, LV_OPA_60, 0);
  lv_obj_set_style_pad_top(done_label, 6, 0);
  lv_obj_add_flag(done_label, LV_OBJ_FLAG_HIDDEN);

  // lunch divider: faint rules flanking a fork/knife glyph + "Lunch break";
  // restyle() positions it at the free lunch slot and toggles its visibility
  lunch_line = lv_obj_create(list);
  lv_obj_set_width(lunch_line, lv_pct(100));
  lv_obj_set_height(lunch_line, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(lunch_line, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(lunch_line, 0, 0);
  lv_obj_set_style_pad_all(lunch_line, 0, 0);
  lv_obj_set_style_pad_column(lunch_line, 6, 0);
  lv_obj_set_flex_flow(lunch_line, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(lunch_line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(lunch_line, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(lunch_line, LV_OBJ_FLAG_HIDDEN);

  for (int side = 0; side < 2; side++)
  {
    auto rule = lv_obj_create(lunch_line);
    lv_obj_set_height(rule, 1);
    lv_obj_set_flex_grow(rule, 1);
    lv_obj_set_style_bg_color(rule, lv_color_hex(0x888888), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_40, 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_set_style_radius(rule, 0, 0);
    lv_obj_set_style_pad_all(rule, 0, 0);
    lv_obj_remove_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
    if (side == 0) // insert the icon + label between the two rules
    {
      auto icon = lv_label_create(lunch_line);
      lv_label_set_text(icon, LUNCH_GLYPH);
      lv_obj_set_style_text_font(icon, &font_lunch, 0);
      lv_obj_set_style_text_color(icon, lv_color_hex(0x888888), 0);

      auto text = lv_label_create(lunch_line);
      lv_label_set_text(text, "Lunch break");
      lv_obj_set_style_text_font(text, &lv_font_montserrat_10, 0);
      lv_obj_set_style_text_color(text, lv_color_hex(0x888888), 0);
    }
  }

  debug_panel = lv_obj_create(parent);
  lv_obj_set_width(debug_panel, lv_pct(100));
  lv_obj_set_flex_grow(debug_panel, 1);
  lv_obj_set_style_pad_all(debug_panel, 8, 0);
  lv_obj_set_style_border_width(debug_panel, 0, 0);
  lv_obj_add_flag(debug_panel, LV_OBJ_FLAG_HIDDEN);
  debug_label = lv_label_create(debug_panel);
  lv_obj_set_style_text_font(debug_label, &noto_thai_12, 0);
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

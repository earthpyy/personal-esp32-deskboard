#include "page_claude.h"

#include "../net/claude_client.h"

#include <Arduino.h>

static ClaudeData *data; // heap-allocated in build (dram0 .bss is full)
static bool has_data = false;

static lv_obj_t *stale_btn;
static lv_obj_t *list;
static lv_timer_t *tick_timer;

static constexpr uint32_t STALE_AFTER_MS = 3 * 60 * 1000;

static void set_hidden(lv_obj_t *obj, bool hidden)
{
  if (hidden)
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static lv_color_t severity_color(const char *severity)
{
  if (strcmp(severity, "critical") == 0)
    return lv_palette_main(LV_PALETTE_RED);
  if (strcmp(severity, "warning") == 0)
    return lv_palette_main(LV_PALETTE_ORANGE);
  return lv_palette_main(LV_PALETTE_GREEN);
}

// One labelled bar: "5h  [========]  20%  20:50"
static void add_limit_row(lv_obj_t *parent, const char *name, const ClaudeLimit *limit)
{
  auto row = lv_obj_create(parent);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, 6, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  auto name_label = lv_label_create(row);
  lv_label_set_text(name_label, name);
  lv_obj_set_width(name_label, 34);

  auto bar = lv_bar_create(row);
  lv_obj_set_height(bar, 12);
  lv_obj_set_flex_grow(bar, 1);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, limit->valid ? limit->percent : 0, LV_ANIM_OFF);
  lv_obj_set_style_radius(bar, 3, 0);
  lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(bar, severity_color(limit->severity), LV_PART_INDICATOR);

  auto pct = lv_label_create(row);
  if (limit->valid)
    lv_label_set_text_fmt(pct, "%d%%", limit->percent);
  else
    lv_label_set_text(pct, "--");
  lv_obj_set_width(pct, 38);
  lv_obj_set_style_text_align(pct, LV_TEXT_ALIGN_RIGHT, 0);

  auto reset = lv_label_create(row);
  lv_label_set_text(reset, limit->valid ? limit->resets_label : "");
  lv_obj_set_width(reset, 62);
  lv_obj_set_style_text_font(reset, &noto_thai_12, 0);
  lv_obj_set_style_text_align(reset, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_opa(reset, LV_OPA_70, 0);
}

static void add_account_panel(const ClaudeAccount *acc)
{
  auto card = lv_obj_create(list);
  lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(card, 8, 0);
  lv_obj_set_style_pad_row(card, 5, 0);
  lv_obj_set_style_radius(card, 6, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  auto header = lv_obj_create(card);
  lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  auto name = lv_label_create(header);
  lv_label_set_text(name, acc->label);

  if (!acc->available)
  {
    // dim the whole panel and show why, no bars
    lv_obj_set_style_opa(card, LV_OPA_50, 0);
    auto note = lv_label_create(header);
    lv_label_set_text(note, acc->error[0] != '\0' ? acc->error : "unavailable");
    lv_obj_set_style_text_font(note, &noto_thai_12, 0);
    lv_obj_set_style_text_color(note, lv_palette_main(LV_PALETTE_ORANGE), 0);
    return;
  }

  add_limit_row(card, "5h", &acc->five_hour);
  add_limit_row(card, "Wk", &acc->weekly);
}

static void rebuild()
{
  // clears every child, including any placeholder label — so rebuild owns the
  // placeholder too rather than toggling a now-freed pointer
  lv_obj_clean(list);
  if (data->account_count == 0)
  {
    auto placeholder = lv_label_create(list);
    lv_label_set_text(placeholder, "No accounts");
    lv_obj_set_style_text_opa(placeholder, LV_OPA_60, 0);
    return;
  }
  for (int i = 0; i < data->account_count; i++)
    add_account_panel(&data->accounts[i]);
}

static void update_stale()
{
  auto const last_ok = claude_client_last_success_ms();
  auto const stale = last_ok > 0 && millis() - last_ok > STALE_AFTER_MS;
  set_hidden(stale_btn, !stale);
  if (stale && has_data)
  {
    auto label = lv_obj_get_child(stale_btn, 0);
    lv_label_set_text_fmt(label, LV_SYMBOL_WARNING " as of %s", data->now_label);
  }
}

static void tick_cb(lv_timer_t *timer)
{
  if (claude_client_take_fresh(data))
  {
    has_data = true;
    rebuild();
  }
  update_stale();
}

void page_claude_build(lv_obj_t *parent)
{
  data = new ClaudeData();

  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(parent, 6, 0);
  lv_obj_set_style_pad_row(parent, 6, 0);
  lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  // no title row (the tab icon identifies the page) — this header exists only
  // for the stale indicator and collapses to ~nothing when it's hidden
  auto header = lv_obj_create(parent);
  lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  stale_btn = lv_obj_create(header);
  lv_obj_set_size(stale_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(stale_btn, 2, 0);
  lv_obj_set_style_border_width(stale_btn, 0, 0);
  lv_obj_add_flag(stale_btn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(stale_btn, LV_OBJ_FLAG_SCROLLABLE);
  auto stale_label = lv_label_create(stale_btn);
  lv_obj_set_style_text_font(stale_label, &noto_thai_12, 0);
  lv_obj_set_style_text_color(stale_label, lv_palette_main(LV_PALETTE_ORANGE), 0);
  lv_label_set_text(stale_label, LV_SYMBOL_WARNING);

  list = lv_obj_create(parent);
  lv_obj_set_width(list, lv_pct(100));
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_style_pad_all(list, 2, 0);
  lv_obj_set_style_pad_row(list, 8, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

  // initial placeholder until the first fetch; rebuild() replaces it wholesale
  auto connecting = lv_label_create(list);
  lv_label_set_text(connecting, "Connecting...");
  lv_obj_set_style_text_opa(connecting, LV_OPA_60, 0);

  tick_timer = lv_timer_create(tick_cb, 1000, nullptr);
  lv_timer_pause(tick_timer);
}

void page_claude_set_active(bool active)
{
  if (tick_timer == nullptr)
    return;
  if (active)
  {
    lv_timer_resume(tick_timer);
    lv_timer_ready(tick_timer);
  }
  else
  {
    lv_timer_pause(tick_timer);
  }
}

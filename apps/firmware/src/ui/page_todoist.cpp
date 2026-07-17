#include "page_todoist.h"

#include "../net/todoist_client.h"

#include <Arduino.h>
#include <string.h>

static TodoistData *data; // heap-allocated in build (dram0 .bss is full)
static bool has_data = false;

static lv_obj_t *filter_label;
static lv_obj_t *stale_btn;
static lv_obj_t *list;
static lv_obj_t *toast;
static lv_timer_t *tick_timer;
static lv_timer_t *undo_timer;

// One task's completion sits in a 4s undo window on the board before the POST
// is enqueued, so a stray tap on the resistive touch is cancellable locally.
static bool undo_active = false;
static char undo_id[TODOIST_ID_LEN];

static constexpr uint32_t STALE_AFTER_MS = 3 * 60 * 1000;
static constexpr uint32_t UNDO_WINDOW_MS = 4000;

static void set_hidden(lv_obj_t *obj, bool hidden)
{
  if (hidden)
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

// Todoist priority is 1..4 with 4 highest (UI p1); map to its colour scheme.
static lv_color_t priority_color(uint8_t priority)
{
  switch (priority)
  {
  case 4:
    return lv_palette_main(LV_PALETTE_RED);
  case 3:
    return lv_palette_main(LV_PALETTE_ORANGE);
  case 2:
    return lv_palette_main(LV_PALETTE_BLUE);
  default:
    return lv_palette_main(LV_PALETTE_GREY);
  }
}

// A task under the undo window is optimistically hidden from the list.
static bool is_hidden(const char *id)
{
  return undo_active && strcmp(id, undo_id) == 0;
}

static void checkbox_cb(lv_event_t *e); // completes the tapped task (see below)

static void placeholder(const char *text)
{
  auto lbl = lv_label_create(list);
  lv_label_set_text(lbl, text);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, lv_pct(100));
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(lbl, &noto_thai_12, 0);
  lv_obj_set_style_text_opa(lbl, LV_OPA_60, 0);
  lv_obj_set_style_pad_top(lbl, 20, 0);
}

static void add_task_row(int index)
{
  auto const &t = data->tasks[index];

  auto card = lv_obj_create(list);
  lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(card, 6, 0);
  lv_obj_set_style_pad_column(card, 8, 0);
  lv_obj_set_style_radius(card, 6, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  // checkbox: the ONLY tap target; its ring carries the priority colour
  auto cb = lv_obj_create(card);
  lv_obj_set_size(cb, 20, 20);
  lv_obj_set_style_radius(cb, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(cb, 2, 0);
  lv_obj_set_style_border_color(cb, priority_color(t.priority), 0);
  lv_obj_set_style_bg_opa(cb, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(cb, 0, 0);
  lv_obj_remove_flag(cb, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(cb, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(cb, 8); // resistive touch runs a bit short
  lv_obj_add_event_cb(cb, checkbox_cb, LV_EVENT_CLICKED, (void *)(intptr_t)index);

  auto col = lv_obj_create(card);
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_height(col, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(col, 0, 0);
  lv_obj_set_style_pad_row(col, 2, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(col, LV_OBJ_FLAG_EVENT_BUBBLE); // let a drag on the text scroll the list

  auto content = lv_label_create(col);
  lv_label_set_text(content, t.content);
  lv_label_set_long_mode(content, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(content, lv_pct(100));

  if (t.project[0] != '\0' || t.labels[0] != '\0' || t.due_label[0] != '\0')
  {
    auto meta = lv_obj_create(col);
    lv_obj_set_size(meta, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(meta, 0, 0);
    lv_obj_set_style_pad_column(meta, 6, 0);
    lv_obj_set_style_border_width(meta, 0, 0);
    lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(meta, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(meta, LV_OBJ_FLAG_SCROLLABLE);

    char sub[112];
    if (t.project[0] != '\0' && t.labels[0] != '\0')
      lv_snprintf(sub, sizeof(sub), "%s  %s", t.project, t.labels);
    else
      lv_snprintf(sub, sizeof(sub), "%s", t.project[0] != '\0' ? t.project : t.labels);
    auto sub_label = lv_label_create(meta);
    lv_label_set_text(sub_label, sub);
    lv_label_set_long_mode(sub_label, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(sub_label, 1);
    lv_obj_set_style_text_font(sub_label, &noto_thai_12, 0);
    lv_obj_set_style_text_opa(sub_label, LV_OPA_60, 0);

    if (t.due_label[0] != '\0')
    {
      auto due = lv_label_create(meta);
      lv_label_set_text(due, t.due_label);
      lv_obj_set_style_text_font(due, &noto_thai_12, 0);
      if (t.overdue)
        lv_obj_set_style_text_color(due, lv_palette_main(LV_PALETTE_RED), 0);
      else
        lv_obj_set_style_text_opa(due, LV_OPA_60, 0);
    }
  }
}

static void rebuild()
{
  lv_obj_clean(list);

  if (!has_data)
  {
    placeholder("Connecting...");
    return;
  }
  if (!data->configured)
  {
    placeholder("Add a Todoist token and pick a filter in the admin UI.");
    return;
  }
  if (data->error[0] != '\0' && data->task_count == 0)
  {
    placeholder(data->error);
    return;
  }

  int shown = 0;
  for (int i = 0; i < data->task_count; i++)
  {
    if (is_hidden(data->tasks[i].id))
      continue;
    add_task_row(i);
    shown++;
  }
  if (shown == 0)
    placeholder("All clear!");
}

// Enqueue the pending completion to the fetch task and close the undo window.
// The task stays hidden; the follow-up poll confirms it's gone (or, if the POST
// failed, brings it back).
static void commit_pending()
{
  if (!undo_active)
    return;
  todoist_client_complete(undo_id);
  undo_active = false;
  lv_timer_pause(undo_timer);
}

static void begin_undo(const char *id)
{
  if (undo_active)
    commit_pending(); // only one undo in flight; commit the previous one
  strlcpy(undo_id, id, sizeof(undo_id));
  undo_active = true;
  lv_timer_reset(undo_timer);
  lv_timer_resume(undo_timer);
  set_hidden(toast, false);
  rebuild(); // the completed row disappears immediately
}

static void checkbox_cb(lv_event_t *e)
{
  auto const index = (int)(intptr_t)lv_event_get_user_data(e);
  if (!has_data || index < 0 || index >= data->task_count)
    return;
  begin_undo(data->tasks[index].id);
}

static void undo_timer_cb(lv_timer_t *)
{
  commit_pending();
  set_hidden(toast, true);
}

static void undo_button_cb(lv_event_t *)
{
  undo_active = false;
  lv_timer_pause(undo_timer);
  set_hidden(toast, true);
  rebuild(); // the row returns
}

static void update_filter_label()
{
  lv_label_set_text(filter_label,
                    has_data && data->filter_name[0] != '\0' ? data->filter_name : "Todoist");
}

static void update_stale()
{
  auto const last_ok = todoist_client_last_success_ms();
  auto const stale = last_ok > 0 && millis() - last_ok > STALE_AFTER_MS;
  set_hidden(stale_btn, !stale);
  if (stale && has_data)
  {
    auto label = lv_obj_get_child(stale_btn, 0);
    lv_label_set_text_fmt(label, LV_SYMBOL_WARNING " as of %s", data->now_label);
  }
}

static void tick_cb(lv_timer_t *)
{
  if (todoist_client_take_fresh(data))
  {
    has_data = true;
    rebuild();
    update_filter_label();
  }
  update_stale();
}

void page_todoist_build(lv_obj_t *parent)
{
  data = new TodoistData();

  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(parent, 6, 0);
  lv_obj_set_style_pad_row(parent, 4, 0);
  lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  auto header = lv_obj_create(parent);
  lv_obj_set_size(header, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(header, 2, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  filter_label = lv_label_create(header);
  lv_label_set_text(filter_label, "Todoist");
  lv_label_set_long_mode(filter_label, LV_LABEL_LONG_DOT);
  lv_obj_set_flex_grow(filter_label, 1);

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
  lv_obj_set_style_pad_row(list, 6, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

  placeholder("Connecting...");

  // undo toast floats over the bottom of the list; hidden until a completion
  toast = lv_obj_create(parent);
  lv_obj_add_flag(toast, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(toast, lv_pct(96), LV_SIZE_CONTENT);
  lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_set_style_pad_hor(toast, 12, 0);
  lv_obj_set_style_pad_ver(toast, 6, 0);
  lv_obj_set_style_radius(toast, 8, 0);
  lv_obj_set_style_border_width(toast, 0, 0);
  lv_obj_set_style_bg_color(toast, lv_color_hex(0x2A2A2A), 0);
  lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(toast, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(toast, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(toast, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);

  auto toast_label = lv_label_create(toast);
  lv_label_set_text(toast_label, "Task completed");
  lv_obj_set_style_text_font(toast_label, &noto_thai_12, 0);
  lv_obj_set_style_text_color(toast_label, lv_color_white(), 0);

  auto undo_btn = lv_button_create(toast);
  lv_obj_set_style_pad_hor(undo_btn, 10, 0);
  lv_obj_set_style_pad_ver(undo_btn, 4, 0);
  lv_obj_set_ext_click_area(undo_btn, 6);
  lv_obj_add_event_cb(undo_btn, undo_button_cb, LV_EVENT_CLICKED, nullptr);
  auto undo_lbl = lv_label_create(undo_btn);
  lv_label_set_text(undo_lbl, "UNDO");

  undo_timer = lv_timer_create(undo_timer_cb, UNDO_WINDOW_MS, nullptr);
  lv_timer_pause(undo_timer);

  tick_timer = lv_timer_create(tick_cb, 1000, nullptr);
  lv_timer_pause(tick_timer);
}

void page_todoist_set_active(bool active)
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
    // can't undo what you can't see — commit any pending completion on exit
    if (undo_active)
    {
      commit_pending();
      set_hidden(toast, true);
    }
  }
}

#include "ui.h"

#include "page_monitor.h"
#include "page_schedule.h"

#include <lvgl.h>

// Adding a tab = one entry here (icon + build + optional set_active; use
// nullptr for pages without per-activation behavior). Note: LVGL's symbol
// font has no chart glyph, so Monitoring uses LV_SYMBOL_CHARGE.
struct TabDef
{
  const char *icon;
  void (*build)(lv_obj_t *parent);
  void (*set_active)(bool active);
};

static const TabDef tabs[] = {
    {LV_SYMBOL_LIST, page_schedule_build, page_schedule_set_active},
    {LV_SYMBOL_CHARGE, page_monitor_build, page_monitor_set_active},
};
static constexpr size_t tab_count = sizeof(tabs) / sizeof(tabs[0]);

static void tab_changed_cb(lv_event_t *e)
{
  auto tabview = (lv_obj_t *)lv_event_get_target(e);
  auto const active = lv_tabview_get_tab_active(tabview);
  for (size_t i = 0; i < tab_count; i++)
    if (tabs[i].set_active != nullptr)
      tabs[i].set_active(i == active);
}

void ui_init()
{
  auto tabview = lv_tabview_create(lv_screen_active());
  lv_tabview_set_tab_bar_position(tabview, LV_DIR_BOTTOM);
  lv_tabview_set_tab_bar_size(tabview, 48);

  // switch tabs via the bar only; page content handles its own scrolling
  lv_obj_remove_flag(lv_tabview_get_content(tabview), LV_OBJ_FLAG_SCROLLABLE);

  for (size_t i = 0; i < tab_count; i++)
  {
    auto content = lv_tabview_add_tab(tabview, tabs[i].icon);
    tabs[i].build(content);
  }

  lv_obj_add_event_cb(tabview, tab_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);

  // activate the initial tab's page
  if (tabs[0].set_active != nullptr)
    tabs[0].set_active(true);
}

#include "ui.h"

#include "page_claude.h"
#include "page_monitor.h"
#include "page_schedule.h"

#include <lvgl.h>

// FontAwesome "calendar" (U+F073); the built-in symbol font lacks it, so it
// ships as a one-glyph font (src/ui/font_calendar.c) applied to the tab label.
LV_FONT_DECLARE(font_calendar);
#define ICON_CALENDAR "\xEF\x81\xB3"

// The Claude "spark" mark (U+E000), hand-rasterized one-glyph font
// (src/ui/font_claude.c).
LV_FONT_DECLARE(font_claude);
#define ICON_CLAUDE "\xEE\x80\x80"

// Adding a tab = one entry here (icon + optional icon_font + build + optional
// set_active; use nullptr for the default symbol font / no per-activation
// behavior). Note: LVGL's symbol font has no chart glyph, so Monitoring uses
// LV_SYMBOL_CHARGE.
struct TabDef
{
  const char *icon;
  const lv_font_t *icon_font; // nullptr = default symbol font
  void (*build)(lv_obj_t *parent);
  void (*set_active)(bool active);
};

static const TabDef tabs[] = {
    {ICON_CALENDAR, &font_calendar, page_schedule_build, page_schedule_set_active},
    {ICON_CLAUDE, &font_claude, page_claude_build, page_claude_set_active},
    {LV_SYMBOL_CHARGE, nullptr, page_monitor_build, page_monitor_set_active},
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
    if (tabs[i].icon_font != nullptr)
    {
      // tab bar button -> its label (child 0); give it the custom icon font
      auto button = lv_obj_get_child(lv_tabview_get_tab_bar(tabview), i);
      lv_obj_set_style_text_font(lv_obj_get_child(button, 0), tabs[i].icon_font, 0);
    }
  }

  lv_obj_add_event_cb(tabview, tab_changed_cb, LV_EVENT_VALUE_CHANGED, nullptr);

  // activate the initial tab's page
  if (tabs[0].set_active != nullptr)
    tabs[0].set_active(true);
}

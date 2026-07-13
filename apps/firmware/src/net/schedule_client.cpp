#include "schedule_client.h"

#include "net.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <secrets.h>

// heap-allocated in init: three ~5 KB ScheduleData instances live across the
// firmware, and as statics they overflow the ESP32's dram0 .bss segment
static SemaphoreHandle_t data_mutex;
static ScheduleData *shared;
static ScheduleData *scratch;
static bool fresh = false;
static volatile uint32_t last_success_ms = 0;

static uint32_t parse_color(const char *hex)
{
  if (hex == nullptr || hex[0] != '#')
    return 0x888888;
  return strtoul(hex + 1, nullptr, 16);
}

static void copy_str(char *dst, size_t cap, const char *src)
{
  strlcpy(dst, src != nullptr ? src : "", cap);
}

// like copy_str, but marks a too-long title with a trailing "..." instead of
// silently cutting it. trims on a UTF-8 boundary so multi-byte (Thai) glyphs
// are never split into tofu.
static void copy_title(char *dst, size_t cap, const char *src)
{
  if (src == nullptr)
    src = "";
  size_t const len = strlen(src);
  if (len < cap) // fits with its NUL, no trimming needed
  {
    memcpy(dst, src, len + 1);
    return;
  }
  size_t keep = cap - 4; // leave room for "..." + NUL
  while (keep > 0 && (static_cast<unsigned char>(src[keep]) & 0xC0) == 0x80)
    keep--; // back off a continuation byte so we don't split a code point
  memcpy(dst, src, keep);
  memcpy(dst + keep, "...", 4); // copies the trailing NUL too
}

static bool fetch(ScheduleData *out)
{
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(String(API_BASE_URL) + "/schedule"))
    return false;
  http.addHeader("X-Board-IP", net_ip_str());
  auto const code = http.GET();
  if (code != HTTP_CODE_OK)
  {
    log_w("schedule fetch failed: %d", code);
    http.end();
    return false;
  }
  JsonDocument doc;
  auto const err = deserializeJson(doc, http.getString());
  http.end();
  if (err)
  {
    log_w("schedule parse failed: %s", err.c_str());
    return false;
  }

  out->server_now = doc["now"].as<long long>();
  out->received_ms = millis();
  copy_str(out->date_label, sizeof(out->date_label), doc["date"]);
  copy_str(out->now_label, sizeof(out->now_label), doc["now_label"]);

  out->all_day_count = 0;
  for (JsonObject ev : doc["all_day"].as<JsonArray>())
  {
    if (out->all_day_count >= SCHEDULE_MAX_ALL_DAY)
      break;
    auto &slot = out->all_day[out->all_day_count++];
    copy_title(slot.title, sizeof(slot.title), ev["title"]);
    copy_str(slot.calendar, sizeof(slot.calendar), ev["calendar"]);
    slot.color = parse_color(ev["color"]);
  }

  out->event_count = 0;
  for (JsonObject ev : doc["events"].as<JsonArray>())
  {
    if (out->event_count >= SCHEDULE_MAX_EVENTS)
      break;
    auto &slot = out->events[out->event_count++];
    copy_title(slot.title, sizeof(slot.title), ev["title"]);
    copy_str(slot.time_label, sizeof(slot.time_label), ev["time"]);
    copy_str(slot.calendar, sizeof(slot.calendar), ev["calendar"]);
    slot.color = parse_color(ev["color"]);
    slot.start = ev["start"].as<long long>();
    slot.end = ev["end"].as<long long>();
  }
  return true;
}

static void fetch_task(void *)
{
  for (;;)
  {
    bool ok = false;
    if (net_connected())
      ok = fetch(scratch);
    if (ok)
    {
      xSemaphoreTake(data_mutex, portMAX_DELAY);
      *shared = *scratch;
      fresh = true;
      xSemaphoreGive(data_mutex);
      last_success_ms = millis();
    }
    // fast retry until the first success so boot isn't stuck for a minute
    auto const delay_ms = last_success_ms == 0 ? 5000 : 60000;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  }
}

void schedule_client_init()
{
  shared = new ScheduleData();
  scratch = new ScheduleData();
  data_mutex = xSemaphoreCreateMutex();
  // core 0: LVGL + the Arduino loop own core 1
  xTaskCreatePinnedToCore(fetch_task, "sched_fetch", 12288, nullptr, 1, nullptr, 0);
}

bool schedule_client_take_fresh(ScheduleData *out)
{
  bool got = false;
  xSemaphoreTake(data_mutex, portMAX_DELAY);
  if (fresh)
  {
    *out = *shared;
    fresh = false;
    got = true;
  }
  xSemaphoreGive(data_mutex);
  return got;
}

uint32_t schedule_client_last_success_ms()
{
  return last_success_ms;
}

int64_t schedule_client_now(const ScheduleData *data)
{
  return data->server_now + (int64_t)((millis() - data->received_ms) / 1000);
}

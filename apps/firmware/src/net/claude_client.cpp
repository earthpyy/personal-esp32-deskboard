#include "claude_client.h"

#include "net.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <secrets.h>

// Same core-0 fetch + mutex/fresh-flag hand-off as schedule_client; the UI task
// on core 1 only ever touches a copied ClaudeData.
static SemaphoreHandle_t data_mutex;
static ClaudeData *shared;
static ClaudeData *scratch;
static bool fresh = false;
static volatile uint32_t last_success_ms = 0;

static void copy_str(char *dst, size_t cap, const char *src)
{
  strlcpy(dst, src != nullptr ? src : "", cap);
}

static void parse_limit(JsonVariant src, ClaudeLimit *out)
{
  if (src.isNull())
  {
    out->valid = false;
    out->percent = 0;
    out->severity[0] = '\0';
    out->resets_label[0] = '\0';
    return;
  }
  out->valid = true;
  out->percent = src["percent"].as<uint8_t>();
  copy_str(out->severity, sizeof(out->severity), src["severity"]);
  copy_str(out->resets_label, sizeof(out->resets_label), src["resets_label"]);
}

static bool fetch(ClaudeData *out)
{
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(String(API_BASE_URL) + "/claude"))
    return false;
  http.addHeader("X-Board-IP", net_ip_str());
  auto const code = http.GET();
  if (code != HTTP_CODE_OK)
  {
    log_w("claude fetch failed: %d", code);
    http.end();
    return false;
  }
  JsonDocument doc;
  auto const err = deserializeJson(doc, http.getString());
  http.end();
  if (err)
  {
    log_w("claude parse failed: %s", err.c_str());
    return false;
  }

  out->server_now = doc["now"].as<long long>();
  out->received_ms = millis();
  copy_str(out->now_label, sizeof(out->now_label), doc["now_label"]);

  out->account_count = 0;
  for (JsonObject acc : doc["accounts"].as<JsonArray>())
  {
    if (out->account_count >= CLAUDE_MAX_ACCOUNTS)
      break;
    auto &slot = out->accounts[out->account_count++];
    copy_str(slot.label, sizeof(slot.label), acc["label"]);
    slot.available = acc["available"].as<bool>();
    copy_str(slot.error, sizeof(slot.error), acc["error"]);
    parse_limit(acc["five_hour"], &slot.five_hour);
    parse_limit(acc["weekly"], &slot.weekly);
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

void claude_client_init()
{
  shared = new ClaudeData();
  scratch = new ClaudeData();
  data_mutex = xSemaphoreCreateMutex();
  // core 0: LVGL + the Arduino loop own core 1
  xTaskCreatePinnedToCore(fetch_task, "claude_fetch", 8192, nullptr, 1, nullptr, 0);
}

bool claude_client_take_fresh(ClaudeData *out)
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

uint32_t claude_client_last_success_ms()
{
  return last_success_ms;
}

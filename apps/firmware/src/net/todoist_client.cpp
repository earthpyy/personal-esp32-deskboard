#include "todoist_client.h"

#include "net.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <secrets.h>

// Same core-0 fetch + mutex/fresh-flag hand-off as schedule_client, plus a small
// queue of task ids to complete: the UI enqueues, this task POSTs them (the only
// board→API write) then refreshes. The UI task on core 1 only touches a copy.
static SemaphoreHandle_t data_mutex;
static QueueHandle_t complete_queue; // items are char[TODOIST_ID_LEN]
static TodoistData *shared;
static TodoistData *scratch;
static bool fresh = false;
static volatile uint32_t last_success_ms = 0;

static void copy_str(char *dst, size_t cap, const char *src)
{
  strlcpy(dst, src != nullptr ? src : "", cap);
}

static bool fetch(TodoistData *out)
{
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(String(API_BASE_URL) + "/todoist"))
    return false;
  http.addHeader("X-Board-IP", net_ip_str());
  auto const code = http.GET();
  if (code != HTTP_CODE_OK)
  {
    log_w("todoist fetch failed: %d", code);
    http.end();
    return false;
  }
  JsonDocument doc;
  // stream straight off the socket: no full-body String allocation
  auto const err = deserializeJson(doc, http.getStream());
  http.end();
  if (err)
  {
    log_w("todoist parse failed: %s", err.c_str());
    return false;
  }

  out->server_now = doc["now"].as<long long>();
  out->received_ms = millis();
  copy_str(out->now_label, sizeof(out->now_label), doc["now_label"]);
  copy_str(out->filter_name, sizeof(out->filter_name), doc["filter_name"]);
  copy_str(out->error, sizeof(out->error), doc["error"]);
  out->configured = doc["configured"].as<bool>();

  out->task_count = 0;
  for (JsonObject tsk : doc["tasks"].as<JsonArray>())
  {
    if (out->task_count >= TODOIST_MAX_TASKS)
      break;
    auto &slot = out->tasks[out->task_count++];
    copy_str(slot.id, sizeof(slot.id), tsk["id"]);
    copy_str(slot.content, sizeof(slot.content), tsk["content"]);
    copy_str(slot.project, sizeof(slot.project), tsk["project"]);
    copy_str(slot.labels, sizeof(slot.labels), tsk["labels"]);
    copy_str(slot.due_label, sizeof(slot.due_label), tsk["due_label"]);
    slot.priority = tsk["priority"].as<uint8_t>();
    slot.overdue = tsk["overdue"].as<bool>();
  }
  return true;
}

static void post_complete(const char *id)
{
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(String(API_BASE_URL) + "/todoist/complete"))
    return;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Board-IP", net_ip_str());
  // ids are numeric Todoist strings, no JSON escaping needed
  String body = String("{\"id\":\"") + id + "\"}";
  auto const code = http.POST(body);
  http.end();
  if (code != HTTP_CODE_OK)
    log_w("todoist complete failed: %d", code);
}

static void fetch_task(void *)
{
  for (;;)
  {
    char id[TODOIST_ID_LEN];
    // fast retry until the first success so boot isn't stuck for a minute
    auto const delay_ms = last_success_ms == 0 ? 5000 : 60000;
    // wake early if a completion is queued, else poll on the interval
    if (xQueueReceive(complete_queue, id, pdMS_TO_TICKS(delay_ms)) == pdTRUE)
    {
      if (net_connected())
        post_complete(id);
      // drain any others enqueued in the meantime before refreshing
      while (xQueueReceive(complete_queue, id, 0) == pdTRUE)
        if (net_connected())
          post_complete(id);
    }

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
  }
}

void todoist_client_init()
{
  shared = new TodoistData();
  scratch = new TodoistData();
  data_mutex = xSemaphoreCreateMutex();
  complete_queue = xQueueCreate(8, TODOIST_ID_LEN);
  // core 0: LVGL + the Arduino loop own core 1. 4 KB stack: measured peak use
  // is ~2 KB (JSON is streamed off the socket into the heap, not the stack).
  xTaskCreatePinnedToCore(fetch_task, "todoist_fetch", 4096, nullptr, 1, nullptr, 0);
}

bool todoist_client_take_fresh(TodoistData *out)
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

uint32_t todoist_client_last_success_ms()
{
  return last_success_ms;
}

void todoist_client_complete(const char *id)
{
  char buf[TODOIST_ID_LEN];
  strlcpy(buf, id, sizeof(buf));
  xQueueSend(complete_queue, buf, 0);
}

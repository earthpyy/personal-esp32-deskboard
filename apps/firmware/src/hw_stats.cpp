#include "hw_stats.h"

#include <Arduino.h>
#include <esp_freertos_hooks.h>

// CPU % is an estimate: the Arduino core doesn't enable FreeRTOS runtime
// stats, so we count idle-task iterations per core and self-calibrate against
// the highest rate ever observed (~fully idle).
static volatile uint32_t idle_counts[2] = {0, 0};
static float idle_max_per_s[2] = {0.0f, 0.0f};
static uint32_t last_sample_ms = 0;

static bool idle_hook()
{
  idle_counts[xPortGetCoreID()]++;
  return true;
}

void hw_stats_init()
{
  last_sample_ms = millis();
  esp_register_freertos_idle_hook_for_cpu(idle_hook, 0);
  esp_register_freertos_idle_hook_for_cpu(idle_hook, 1);
}

void hw_stats_sample(HwStats *out)
{
  auto const now = millis();
  auto const dt_ms = now - last_sample_ms;
  last_sample_ms = now;

  for (int core = 0; core < 2; core++)
  {
    uint32_t const count = idle_counts[core];
    idle_counts[core] = 0;

    if (dt_ms == 0)
    {
      out->cpu_pct[core] = -1.0f;
      continue;
    }

    float const per_s = count * 1000.0f / dt_ms;
    if (per_s > idle_max_per_s[core])
      idle_max_per_s[core] = per_s;

    if (idle_max_per_s[core] > 0.0f)
      out->cpu_pct[core] = 100.0f * (1.0f - per_s / idle_max_per_s[core]);
    else
      out->cpu_pct[core] = -1.0f;
  }

  out->heap_free = esp_get_free_heap_size();
  out->heap_min_free = esp_get_minimum_free_heap_size();
  out->heap_total = ESP.getHeapSize();
  out->uptime_s = now / 1000;
}

void hw_stats_chip_info(ChipInfo *out)
{
  out->model = ESP.getChipModel();
  out->revision = ESP.getChipRevision();
  out->cpu_mhz = ESP.getCpuFreqMHz();
  out->flash_size = ESP.getFlashChipSize();
}

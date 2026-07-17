#pragma once

#include <stdint.h>

struct HwStats
{
  float cpu_pct[2]; // per-core usage estimate, -1.0f if not calibrated yet
  uint32_t heap_free;
  uint32_t heap_min_free;
  uint32_t heap_total;
  uint32_t uptime_s;
};

struct ChipInfo
{
  const char *model;
  uint16_t revision;
  uint32_t cpu_mhz;
  uint32_t flash_size;
};

void hw_stats_init();
void hw_stats_sample(HwStats *out);
void hw_stats_chip_info(ChipInfo *out);

// Phase 1 heap instrumentation: logs free / min-free / largest-free-block over
// serial. The gap between free and largest-free-block is the fragmentation
// signal. Self-throttled — safe to call every loop().
void hw_stats_log_heap();

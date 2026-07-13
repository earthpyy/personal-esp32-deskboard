#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr size_t SCHEDULE_MAX_EVENTS = 30;
constexpr size_t SCHEDULE_MAX_ALL_DAY = 10;

struct ScheduleEvent
{
  char title[128];
  char time_label[24];
  char calendar[32];
  uint32_t color;
  int64_t start; // epoch seconds, clamped to today by the server
  int64_t end;
};

struct ScheduleAllDay
{
  char title[128];
  char calendar[32];
  uint32_t color;
};

struct ScheduleData
{
  char date_label[24];
  char now_label[8]; // server-local "HH:MM" at fetch time, for the stale header
  ScheduleAllDay all_day[SCHEDULE_MAX_ALL_DAY];
  uint8_t all_day_count;
  ScheduleEvent events[SCHEDULE_MAX_EVENTS];
  uint8_t event_count;
  int64_t server_now;   // epoch seconds when the server answered
  uint32_t received_ms; // millis() when we got it; clock anchor
};

void schedule_client_init();
bool schedule_client_take_fresh(ScheduleData *out);
uint32_t schedule_client_last_success_ms();
int64_t schedule_client_now(const ScheduleData *data);

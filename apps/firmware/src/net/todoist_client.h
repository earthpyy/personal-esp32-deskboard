#pragma once

#include <stddef.h>
#include <stdint.h>

// Kept small on purpose: three TodoistData buffers exist at once (shared +
// scratch in the client, one copy in the page), and this is a WROOM-32 with no
// PSRAM, so every byte here is tripled against a tight internal heap. 20 tasks
// is plenty for a 320x240 scrollable list, and the server pre-sorts so the ones
// the board keeps are the most important.
constexpr size_t TODOIST_MAX_TASKS = 16;
constexpr size_t TODOIST_ID_LEN = 32;

struct TodoistTask
{
  char id[TODOIST_ID_LEN];
  char content[112];
  char project[32];
  char labels[40];       // "@a @b", empty when none
  char due_label[24];    // server pre-formatted, e.g. "Today", "Tomorrow 09:00"
  uint8_t priority;      // 1..4, 4 = highest (Todoist p1)
  bool overdue;
};

struct TodoistData
{
  char now_label[8];     // server-local "HH:MM" at fetch time, for the stale header
  char filter_name[40];  // the selected filter's name
  char error[24];        // short reason when the feed can't be built (e.g. "no token")
  bool configured;       // token + a filter both set in the admin UI
  TodoistTask tasks[TODOIST_MAX_TASKS];
  uint8_t task_count;
  int64_t server_now;    // epoch seconds when the server answered
  uint32_t received_ms;  // millis() when we got it; clock anchor
};

void todoist_client_init();
bool todoist_client_take_fresh(TodoistData *out);
uint32_t todoist_client_last_success_ms();

// Enqueue a task completion. The POST runs on the core-0 fetch task, followed by
// an immediate refresh so the board drops the task. Safe to call from the UI.
void todoist_client_complete(const char *id);

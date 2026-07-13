#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr size_t CLAUDE_MAX_ACCOUNTS = 4;

// A single rolling-limit window (5-hour or weekly) for one account. `valid` is
// false when the account has no window data (e.g. token unavailable).
struct ClaudeLimit
{
  bool valid;
  uint8_t percent;
  char severity[10];      // "normal" | "warning" | "critical"
  char resets_label[16];  // server pre-formatted, e.g. "20:50" or "Sun 18:00"
};

struct ClaudeAccount
{
  char label[24];
  bool available;         // false = token expired / unreadable
  char error[24];         // short reason when unavailable
  ClaudeLimit five_hour;
  ClaudeLimit weekly;
};

struct ClaudeData
{
  char now_label[8]; // server-local "HH:MM" at fetch time, for the stale header
  ClaudeAccount accounts[CLAUDE_MAX_ACCOUNTS];
  uint8_t account_count;
  int64_t server_now;   // epoch seconds when the server answered
  uint32_t received_ms; // millis() when we got it; clock anchor
};

void claude_client_init();
bool claude_client_take_fresh(ClaudeData *out);
uint32_t claude_client_last_success_ms();

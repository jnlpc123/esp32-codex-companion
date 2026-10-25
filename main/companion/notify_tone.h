#pragma once
#include <stdbool.h>
#include <stdint.h>

void notify_tone_init(int volume, bool muted);
void notify_tone_settings(int volume, bool muted);
bool notify_tone_play(bool waiting);
bool notify_tone_available(void);
uint32_t notify_tone_samples(void);

bool notify_tone_play_kind(int kind);

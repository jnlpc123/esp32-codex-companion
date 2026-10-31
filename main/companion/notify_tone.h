#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { NOTIFY_ORIGINAL, NOTIFY_8BIT } notify_sound_style_t;
void notify_tone_init(int volume, bool muted, notify_sound_style_t style);
void notify_tone_settings(int volume, bool muted, notify_sound_style_t style);
bool notify_tone_play(bool waiting);
bool notify_tone_available(void);
uint32_t notify_tone_samples(void);

bool notify_tone_play_kind(int kind);

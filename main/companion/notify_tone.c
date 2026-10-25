#include "notify_tone.h"
#include "audio/es8311.h"
#include "core/config.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static es8311_handle_t s_codec;
static QueueHandle_t s_notes;
static volatile int s_volume;
static volatile bool s_muted;
static volatile uint32_t s_samples;

static void note(unsigned frequency, unsigned milliseconds)
{
    int16_t pcm[256 * 2];
    uint32_t phase = 0;
    uint32_t step = (uint64_t)frequency * (UINT64_C(1) << 32) / ES8311_SAMPLE_RATE;
    unsigned total = ES8311_SAMPLE_RATE * milliseconds / 1000;
    const unsigned ramp = ES8311_SAMPLE_RATE / 100;
    for (unsigned first = 0; first < total && !s_muted; first += 256) {
        unsigned count = total - first < 256 ? total - first : 256;
        for (unsigned i = 0; i < count; i++) {
            int32_t p = phase >> 16;
            int32_t triangle = p < 32768 ? p * 2 - 32768 : 98303 - p * 2;
            int32_t sample = frequency ? triangle * 2200 / 32768 : 0;
            unsigned position = first + i;
            unsigned envelope = position < ramp ? position : total - position < ramp ? total - position : ramp;
            sample = sample * envelope / ramp;
            pcm[i * 2] = pcm[i * 2 + 1] = sample;
            phase += step;
        }
        size_t written = 0;
        if (i2s_channel_write(es8311_get_tx_handle(s_codec), pcm, count * 4, &written, 200) != ESP_OK) break;
        s_samples += written / 4;
    }
}

static void tone_task(void *unused)
{
    int waiting;
    while (1) {
        if (xQueueReceive(s_notes, &waiting, portMAX_DELAY) != pdTRUE || s_muted) continue;
        es8311_set_volume(s_codec, s_volume);
        if (waiting==0) {
            note(880, 110); note(0, 65); note(660, 150);
        } else if(waiting==1) {note(1047,90);
        } else {
            note(1047, 90); note(0, 35); note(1319, 90); note(0, 35); note(1568, 160);
        }
        note(0, 20);
    }
}

void notify_tone_settings(int volume, bool muted)
{
    s_volume = volume;
    s_muted = muted || volume == 0;
}

void notify_tone_init(int volume, bool muted)
{
    notify_tone_settings(volume, muted);
    s_codec = es8311_init(volume);
    if (!s_codec) return;
    s_notes = xQueueCreate(1, sizeof(int));
    if (!s_notes || xTaskCreate(tone_task, "vibe_tone", 4096, NULL, 4, NULL) != pdPASS) {
        if (s_notes) vQueueDelete(s_notes);
        s_notes = NULL;
        es8311_deinit(s_codec);
        s_codec = NULL;
    }
}

bool notify_tone_available(void) { return s_codec && s_notes; }
uint32_t notify_tone_samples(void) { return s_samples; }
bool notify_tone_play(bool waiting){return notify_tone_play_kind(waiting?0:2);}
bool notify_tone_play_kind(int waiting)
{
    return notify_tone_available() && !s_muted && xQueueOverwrite(s_notes, &waiting) == pdTRUE;
}

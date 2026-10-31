#include "notify_tone.h"
#include "audio/es8311.h"
#include "core/config.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "notify_wave.h"

static es8311_handle_t s_codec;
static QueueHandle_t s_notes;
static volatile int s_volume;
static volatile bool s_muted;
static volatile notify_sound_style_t s_style;
static volatile uint32_t s_samples;

static void note(unsigned frequency, unsigned milliseconds, notify_sound_style_t style)
{
    int16_t pcm[256 * 2];
    uint32_t phase = 0;
    uint32_t step = (uint64_t)frequency * (UINT64_C(1) << 32) / ES8311_SAMPLE_RATE;
    unsigned total = ES8311_SAMPLE_RATE * milliseconds / 1000;
    const unsigned attack = ES8311_SAMPLE_RATE * (style==NOTIFY_8BIT?2:10) / 1000;
    const unsigned release = ES8311_SAMPLE_RATE * (style==NOTIFY_8BIT?8:10) / 1000;
    for (unsigned first = 0; first < total && !s_muted; first += 256) {
        unsigned count = total - first < 256 ? total - first : 256;
        for (unsigned i = 0; i < count; i++) {
            unsigned index=phase>>24, fraction=(phase>>16)&255;
            int32_t a=notify_wave[index],b=notify_wave[(index+1)&255];
            int32_t sample=frequency?(a*256+(b-a)*(int32_t)fraction)*18000/(127*256):0;
            if(style==NOTIFY_ORIGINAL){
                int32_t p=phase>>16;
                int32_t triangle=p<32768?p*2-32768:98303-p*2;
                sample=frequency?triangle*6000/32768:0;
            }
            unsigned position = first + i;
            // Signed arithmetic keeps negative samples negative during fades.
            int32_t fade_in=position<attack?(int32_t)(position*32768/attack):32768;
            int32_t fade_out=total-position<release?(int32_t)((total-position)*32768/release):32768;
            int32_t level=style==NOTIFY_8BIT?5+10*(total-position)/total:15;
            sample=sample*level/15;
            sample=sample*fade_in/32768;
            sample=sample*fade_out/32768;
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
        notify_sound_style_t style=s_style;
        if(style==NOTIFY_ORIGINAL){
            if(waiting==0){note(880,110,style);note(0,65,style);note(660,150,style);}
            else if(waiting==1)note(1047,90,style);
            else{note(1047,90,style);note(0,35,style);note(1319,90,style);note(0,35,style);note(1568,160,style);}
        } else if (waiting==0) {
            note(0,25,style);
            note(784,140,style);note(0,30,style);note(659,180,style);note(0,65,style);
            note(784,120,style);note(0,15,style);note(880,230,style);note(0,85,style);
        } else if(waiting==1) {note(784,120,style);
        } else {
            note(0,25,style);
            note(659,130,style);note(0,15,style);note(784,130,style);note(0,15,style);note(1047,230,style);note(0,80,style);
            note(988,130,style);note(0,20,style);note(784,140,style);note(0,25,style);note(1047,250,style);note(0,110,style);
        }
        note(0,20,style);
    }
}

void notify_tone_settings(int volume, bool muted, notify_sound_style_t style)
{
    s_style=style==NOTIFY_8BIT?NOTIFY_8BIT:NOTIFY_ORIGINAL;
    s_volume = volume;
    s_muted = muted || volume == 0;
}

void notify_tone_init(int volume, bool muted, notify_sound_style_t style)
{
    notify_tone_settings(volume, muted, style);
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

#include "companion_render.h"
#include "companion.h"
#include "animation_cache.h"
#include "alpha_cache.h"
#include "display_init.h"
#include "config.h"
#include "task_dots.h"
#include "frame_transform.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static uint16_t *s_canvas;
static uint32_t s_revision[3];
static uint16_t s_background[3];
static uint16_t s_dot_background[3];
#define CANVAS_PIXELS (DISPLAY_WIDTH*DISPLAY_HEIGHT)
#define FOREGROUND_PIXELS (132*132)
#define FOOTER_PIXELS (TASK_DOT_FOOTER_WIDTH*TASK_DOT_FOOTER_HEIGHT)
_Static_assert(CANVAS_PIXELS-FOREGROUND_PIXELS >= FOOTER_PIXELS,"Footer must not overwrite foreground");
static int s_size[3],s_dots[3];
static esp_err_t footer(int channel,companion_visual_t v)
{
    uint16_t *s_footer=s_canvas+CANVAS_PIXELS-FOOTER_PIXELS;
    for(int i=0;i<TASK_DOT_FOOTER_WIDTH*TASK_DOT_FOOTER_HEIGHT;i++)s_footer[i]=__builtin_bswap16(v.background);
    int n=v.count<5?v.count:5;
    for(int i=0;i<n;i++){
        int cx,cy;task_dot_center(v.count,i,&cx,&cy);cx-=44;cy-=134;
        for(int y=-TASK_DOT_RADIUS;y<=TASK_DOT_RADIUS;y++)for(int x=-TASK_DOT_RADIUS;x<=TASK_DOT_RADIUS;x++){
            int d=x*x+y*y;
            if(d<=25&&(!(v.count>5&&i==4)||d>=9))s_footer[(y+cy)*TASK_DOT_FOOTER_WIDTH+cx+x]=__builtin_bswap16(task_dot_color(v.base_background));
        }
    }
    return display_draw_bitmap_sync(get_display_panel(channel),44,134,116,150,s_footer);
}
static int s_width[3], s_height[3];

esp_err_t companion_render_init(void)
{
    s_canvas = malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * 2);
    return s_canvas ? ESP_OK : ESP_ERR_NO_MEM;
}

bool companion_render_prepare_cache(const char *jpeg_path,char *cache_path,size_t path_size)
{return animation_cache_prepare_into(jpeg_path,cache_path,path_size,s_canvas,CANVAS_PIXELS);}

static esp_err_t background(int channel, uint16_t color, bool preserve_foreground)
{
    uint16_t wire = __builtin_bswap16(color);
    uint16_t *buffer=preserve_foreground ? s_canvas+FOREGROUND_PIXELS : s_canvas;
    int rows=preserve_foreground ? (CANVAS_PIXELS-FOREGROUND_PIXELS)/DISPLAY_WIDTH : DISPLAY_HEIGHT;
    for (int i=0; i<rows*DISPLAY_WIDTH; i++) buffer[i]=wire;
    for (int y=0; y<DISPLAY_HEIGHT; y+=rows) {
        int end=y+rows<DISPLAY_HEIGHT ? y+rows : DISPLAY_HEIGHT;
        esp_err_t ret=display_draw_bitmap_sync(get_display_panel(channel),0,y,DISPLAY_WIDTH,end,buffer);
        if(ret!=ESP_OK)return ret;
    }
    return ESP_OK;
}

esp_err_t companion_render_frame(int channel, const char *path)
{
    if (!s_canvas || channel < 1 || channel > 3) return ESP_ERR_INVALID_ARG;
    companion_visual_t visual; companion_get_visual(channel, &visual);
    int index = channel - 1;
    bool cleared = !s_revision[index] || s_background[index]!=visual.background || s_size[index]!=visual.foreground_size || (!path&&s_width[index]);
    if (cleared) {
        esp_err_t ret = background(channel, visual.background, false);
        if (ret != ESP_OK) return ret;
        s_background[index]=visual.background;s_size[index]=visual.foreground_size;
        s_width[index] = s_height[index] = 0;
    }
    if(cleared||s_dots[index]!=visual.count||s_dot_background[index]!=visual.base_background){
        esp_err_t ret=footer(channel,visual);if(ret!=ESP_OK)return ret;
        s_dots[index]=visual.count;s_dot_background[index]=visual.base_background;
    }
    s_revision[index]=visual.revision;
    if (!path) return ESP_OK;
    uint16_t width = 0, height = 0;
    const char *ext = strrchr(path, '.');
    esp_err_t ret;
    if (ext && !strcasecmp(ext, ".a5")) {
        alpha_cache_info_t info;
        ret=alpha_cache_decode_file_mode(path,s_canvas,DISPLAY_WIDTH*DISPLAY_HEIGHT,visual.background,&info,!visual.active);
        if(ret==ESP_OK){width=info.width;height=info.height;}
    } else if (ext && !strcmp(ext, ".r5"))
        ret = animation_cache_decode_into(path, s_canvas, DISPLAY_WIDTH * DISPLAY_HEIGHT, &width, &height);
    else {
        ret = display_jpeg_decode_into(path,s_canvas,CANVAS_PIXELS,&width,&height);
    }
    if (ret != ESP_OK || !width || !height) return ret != ESP_OK ? ret : ESP_FAIL;
    if(!visual.active&&(!ext||strcasecmp(ext,".a5")))for(unsigned i=0;i<(unsigned)width*height;i++)s_canvas[i]=__builtin_bswap16(alpha_cache_gray(__builtin_bswap16(s_canvas[i])));
    // Leave a gap above the inward-shifted indicators, including at large sizes.
    int dw = visual.foreground_size>132?132:visual.foreground_size, dh = dw;
    if (width > height) dh = (unsigned)height * dw / width;
    else if (height > width) dw = (unsigned)width * dh / height;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    if(!frame_resize(s_canvas,CANVAS_PIXELS,width,height,dw,dh))return ESP_ERR_INVALID_SIZE;
    display_rotation_t rotation = display_get_rotation();
    int rw = dw, rh = dh;
    if (rotation != DISPLAY_ROTATION_0) {
        if(!frame_rotate(s_canvas,CANVAS_PIXELS,dw,dh,rotation))return ESP_ERR_INVALID_SIZE;
        if (rotation == DISPLAY_ROTATION_90 || rotation == DISPLAY_ROTATION_270) { rw = dh; rh = dw; }
    }
    if (!cleared && s_width[index] && (rw != s_width[index] || rh != s_height[index])) {
        // Mixed-aspect manually selected directories must not leave old pixels.
        ret = background(channel, visual.background, true);
        if (ret != ESP_OK) return ret;
        ret=footer(channel,visual);if(ret!=ESP_OK)return ret;
    }
    int left = (DISPLAY_WIDTH - rw) / 2, top = (144 - rh) / 2;
    if(top+rh>132)top=132-rh;
    ret = display_draw_bitmap_sync(get_display_panel(channel), left, top, left + rw, top + rh, s_canvas);
    if (ret == ESP_OK) { s_width[index] = rw; s_height[index] = rh; }
    return ret;
}

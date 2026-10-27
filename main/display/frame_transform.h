#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Nearest-neighbour scaling with equally directed axes. Reading backwards when
// enlarging keeps every source pixel intact until its last consumer.
static inline bool frame_resize(uint16_t *pixels, size_t capacity,
                                unsigned w, unsigned h, unsigned dw, unsigned dh)
{
    if (!pixels || !w || !h || !dw || !dh || dw>160 || (size_t)w*h > capacity ||
        (size_t)dw*dh > capacity || !((dw >= w && dh >= h) || (dw <= w && dh <= h))) return false;
    if (dw == w && dh == h) return true;
    uint16_t xmap[160];
    for (unsigned x=0; x<dw; x++) xmap[x]=x*w/dw;
    if (dw >= w && dh >= h) {
        for (unsigned y=dh; y-- > 0;) {
            const uint16_t *row=pixels+(size_t)(y*h/dh)*w;
            for (unsigned x=dw; x-- > 0;) pixels[(size_t)y*dw+x]=row[xmap[x]];
        }
    } else {
        for (unsigned y=0; y<dh; y++) {
            const uint16_t *row=pixels+(size_t)(y*h/dh)*w;
            for (unsigned x=0; x<dw; x++) pixels[(size_t)y*dw+x]=row[xmap[x]];
        }
    }
    return true;
}

static inline size_t frame_rotated_index(size_t i, unsigned w, unsigned h, unsigned rotation)
{
    size_t x=i%w,y=i/w;
    return rotation==90 ? x*h+h-1-y : (w-1-x)*h+y;
}

// Quarter turns of rectangles are permutations. Use a bit per pixel, placed in
// the unused tail of the caller's canvas, instead of a second RGB565 frame.
static inline bool frame_rotate(uint16_t *pixels, size_t capacity,
                                unsigned w, unsigned h, unsigned rotation)
{
    size_t count=(size_t)w*h;
    if (!pixels || !w || !h || count>capacity) return false;
    if (rotation==0) return true;
    if (rotation==180) {
        for (size_t i=0; i<count/2; i++) {
            uint16_t v=pixels[i];pixels[i]=pixels[count-1-i];pixels[count-1-i]=v;
        }
        return true;
    }
    if (rotation!=90 && rotation!=270) return false;
    if (w==h) {
        for (unsigned y=0; y<h; y++) for (unsigned x=y+1; x<w; x++) {
            uint16_t v=pixels[(size_t)y*w+x];pixels[(size_t)y*w+x]=pixels[(size_t)x*w+y];pixels[(size_t)x*w+y]=v;
        }
        for (unsigned y=0; y<h; y++) for (unsigned x=0; x<w/2; x++) {
            size_t a=rotation==90 ? (size_t)y*w+x : (size_t)x*w+y;
            size_t b=rotation==90 ? (size_t)y*w+w-1-x : (size_t)(h-1-x)*w+y;
            uint16_t v=pixels[a];pixels[a]=pixels[b];pixels[b]=v;
        }
        return true;
    }
    size_t bytes=(count+7)/8;
    if (bytes>(capacity-count)*sizeof(*pixels)) return false;
    uint8_t *visited=(uint8_t *)(pixels+count);memset(visited,0,bytes);
    for (size_t i=0; i<count; i++) {
        if (visited[i/8]&(1u<<(i%8))) continue;
        size_t at=i;uint16_t value=pixels[at];
        do {
            size_t next=frame_rotated_index(at,w,h,rotation);
            uint16_t replaced=pixels[next];pixels[next]=value;value=replaced;
            visited[at/8]|=1u<<(at%8);at=next;
        } while (at!=i);
    }
    return true;
}

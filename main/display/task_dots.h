#pragma once
#include <stdint.h>

enum { TASK_DOT_RADIUS=5, TASK_DOT_FOOTER_WIDTH=72, TASK_DOT_FOOTER_HEIGHT=16 };

static inline uint16_t task_dot_color(uint16_t background)
{
    return background ^ 0xffff; // Preserve the user's complementary-color scheme.
}

static inline void task_dot_center(int count, int index, int *x, int *y)
{
    int n=count<5?count:5;
    int dx=-(n-1)*7+index*14;
    *x=80+dx;
    *y=144-(dx*dx+74)/148;
}

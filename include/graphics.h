#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "config.h"

struct buffer_info
{
    struct wl_buffer *buffer;
    uint32_t *shm_data;
    int busy;
};

typedef enum {
    left,
    right,
    center
} text_aline;

extern FT_Face ft_face;
extern struct buffer_info *current_draw_buf;

uint64_t get_time_ms(void);
int init_freetype(void);
uint32_t utf8_next_codepoint(const char **p);
void clear_screen(uint32_t color);
void draw_pixel(int x, int y, uint32_t color);
void draw_dot(int cx, int cy, int size, uint32_t color);
void draw_rect(int x, int y, int w, int h, uint32_t color);
void draw_image(int x, int y, int w, int h, const uint32_t *img_data);
void blend_pixel(int x, int y, uint32_t color, uint8_t alpha);
void draw_text(int x, int y, const char *text, int len, text_aline aline, int pixel_size, uint32_t color);
void draw_rect_outline(int x, int y, int w, int h, uint32_t color);
uint32_t* load_image_stb_uint32(const char *path, int *width, int *height);

#endif // GRAPHICS_H

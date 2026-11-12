#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <wayland-client.h>

#include "graphics.h"
#include "util.h"

struct buffer_info *current_draw_buf = NULL;
static FT_Library ft_library = NULL;
FT_Face ft_face = NULL;

void clear_screen(uint32_t color)
{
    if (!current_draw_buf)
        return;
    for (int i = 0; i < WIDTH * HEIGHT; ++i)
    {
        current_draw_buf->shm_data[i] = color;
    }
}

void draw_pixel(int x, int y, uint32_t color)
{
    if (!current_draw_buf)
        return;
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
        return;
    current_draw_buf->shm_data[y * WIDTH + x] = color;
}

void draw_dot(int cx, int cy, int size, uint32_t color)
{
    int half = size / 2;
    for (int dy = -half; dy <= half; dy++)
    {
        for (int dx = -half; dx <= half; dx++)
        {
            draw_pixel(cx + dx, cy + dy, color);
        }
    }
}

void draw_rect(int x, int y, int w, int h, uint32_t color)
{
    for (int row = y; row < y + h; row++)
    {
        for (int col = x; col < x + w; col++)
        {
            draw_pixel(col, row, color);
        }
    }
}

// 現在時刻をミリ秒で取得する
uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int init_freetype(void) {
    if (FT_Init_FreeType(&ft_library)) {
        fprintf(stderr, "Could not init freetype library\n");
        return -1;
    }
    const char *font_paths[] = {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        NULL
    };
    for (int i = 0; font_paths[i]; i++) {
        if (access(font_paths[i], R_OK) == 0) {
            if (FT_New_Face(ft_library, font_paths[i], 0, &ft_face) == 0) {
                debug_log("Loaded font: %s\n", font_paths[i]);
                return 0;
            }
        }
    }
    fprintf(stderr, "Failed to load any font\n");
    return -1;
}

// UTF-8 文字列から 1文字（Unicode コードポイント）を取り出し、バイトオフセットを進める
uint32_t utf8_next_codepoint(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    if (!*s) return 0;

    uint32_t cp = 0;
    int len = 0;
    if ((*s & 0x80) == 0) {
        cp = *s;
        len = 1;
    } else if ((*s & 0xE0) == 0xC0) {
        cp = *s & 0x1F;
        len = 2;
    } else if ((*s & 0xF0) == 0xE0) {
        cp = *s & 0x0F;
        len = 3;
    } else if ((*s & 0xF8) == 0xF0) {
        cp = *s & 0x07;
        len = 4;
    } else {
        *p += 1;
        return '?';
    }

    for (int i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *p += i;
            return '?';
        }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *p += len;
    return cp;
}

// 色のアルファブレンド描画
void blend_pixel(int x, int y, uint32_t color, uint8_t alpha) {
    if (!current_draw_buf || alpha == 0) return;
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;

    if (alpha == 255) {
        current_draw_buf->shm_data[y * WIDTH + x] = color;
        return;
    }

    uint32_t bg = current_draw_buf->shm_data[y * WIDTH + x];
    uint32_t bg_r = (bg >> 16) & 0xFF;
    uint32_t bg_g = (bg >> 8) & 0xFF;
    uint32_t bg_b = bg & 0xFF;

    uint32_t fg_r = (color >> 16) & 0xFF;
    uint32_t fg_g = (color >> 8) & 0xFF;
    uint32_t fg_b = color & 0xFF;

    uint32_t out_r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
    uint32_t out_g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
    uint32_t out_b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;

    current_draw_buf->shm_data[y * WIDTH + x] = (out_r << 16) | (out_g << 8) | out_b;
}

// FreeType を用いたテキスト描画（日本語・UTF-8対応）
void draw_text(int x, int y, const char *text, int len, text_aline aline, int pixel_size, uint32_t color) {
    if (!text || !ft_face) return;
    if (pixel_size < 8) pixel_size = 14;

    FT_Set_Pixel_Sizes(ft_face, 0, pixel_size);

    // テキスト全体の幅を計算
    int total_width = 0;
    const char *p = text;
    while (*p) {
        uint32_t cp = utf8_next_codepoint(&p);
        if (!cp) break;
        if (FT_Load_Char(ft_face, cp, FT_LOAD_DEFAULT)) continue;
        total_width += (ft_face->glyph->advance.x >> 6);
    }

    int start_x = x;
    if (aline == right) {
        start_x = x - total_width;
    } else if (aline == center) {
        start_x = x - (total_width / 2);
    }

    int cur_x = start_x;
    int baseline_y = y + pixel_size; // ベースライン位置

    p = text;
    while (*p) {
        uint32_t cp = utf8_next_codepoint(&p);
        if (!cp) break;

        if (FT_Load_Char(ft_face, cp, FT_LOAD_RENDER)) continue;

        FT_GlyphSlot slot = ft_face->glyph;
        int glyph_x = cur_x + slot->bitmap_left;
        int glyph_y = baseline_y - slot->bitmap_top;

        for (unsigned int row = 0; row < slot->bitmap.rows; row++) {
            for (unsigned int col = 0; col < slot->bitmap.width; col++) {
                uint8_t alpha = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
                if (alpha > 0) {
                    blend_pixel(glyph_x + col, glyph_y + row, color, alpha);
                }
            }
        }
        cur_x += (slot->advance.x >> 6);
    }
}

void draw_rect_outline(int x, int y, int w, int h, uint32_t color)
{
    for (int col = x; col < x + w; col++) {
        draw_pixel(col, y, color);
        draw_pixel(col, y + h - 1, color);
    }
    for (int row = y; row < y + h; row++) {
        draw_pixel(x, row, color);
        draw_pixel(x + w - 1, row, color);
    }
}

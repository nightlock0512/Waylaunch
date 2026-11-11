#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <wayland-client.h>

#include "graphics.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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

uint32_t* load_image_stb_uint32(const char *path, int *width, int *height) {
    // Disable vertical flip; we will keep stb_image's natural top‑left orientation
    stbi_set_flip_vertically_on_load(0);


    int channels;
    // 第4引数に 4 (STBI_rgb_alpha) を指定して強制的にRGBA 8bit/ch で取得
    unsigned char *data = stbi_load(path, width, height, &channels, 4);
    if (!data) {
        return NULL;
    }

    size_t pixel_count = (*width) * (*height);
    uint32_t *buffer = (uint32_t*)malloc(pixel_count * sizeof(uint32_t));

    // Convert RGBA to ARGB (0xAARRGGBB) in a temporary buffer
    uint32_t *tmp_buf = (uint32_t*)malloc(pixel_count * sizeof(uint32_t));
    for (size_t i = 0; i < pixel_count; i++) {
        uint8_t r = data[i * 4 + 0];
        uint8_t g = data[i * 4 + 1];
        uint8_t b = data[i * 4 + 2];
        uint8_t a = data[i * 4 + 3];
        tmp_buf[i] = ((uint32_t)a << 24) |
                     ((uint32_t)r << 16) |
                     ((uint32_t)g << 8)  |
                     (uint32_t)b;
    }

    // Desired icon size
    const int target_w = 32;
    const int target_h = 32;
    // Allocate final buffer
    uint32_t *final_buf = (uint32_t*)malloc(target_w * target_h * sizeof(uint32_t));
    // Nearest‑neighbor scaling
    for (int y = 0; y < target_h; ++y) {
        int src_y = y * (*height) / target_h;
        for (int x = 0; x < target_w; ++x) {
            int src_x = x * (*width) / target_w;
            final_buf[(target_w - y - 1) * target_w + x] = tmp_buf[src_y * (*width) + src_x];
        }
    }

    // Clean up original data
    free(tmp_buf);
    // Update width/height to the standardized size
    *width = target_w;
    *height = target_h;
    return final_buf;
    return buffer;
}

void draw_image(int x, int y, int w, int h, const uint32_t *img_data) {
    if (!img_data || !current_draw_buf || !current_draw_buf->shm_data) return;

    if (x >= WIDTH || y >= HEIGHT || x + w <= 0 || y + h <= 0) return;

    int screen_x_start = (x < 0) ? 0 : x;
    int screen_x_end   = (x + w > WIDTH) ? WIDTH : (x + w);

    int screen_y_start = (y < 0) ? 0 : y;
    int screen_y_end   = (y + h > HEIGHT) ? HEIGHT : (y + h);

    int copy_width = screen_x_end - screen_x_start;
    if (copy_width <= 0) return;

    for (int sy = screen_y_start; sy < screen_y_end; sy++) {
        // img_y is the source row relative to the image's top-left
        int img_y = sy - y;
        // Flip vertically: source row counted from bottom
        int src_y = (h - 1) - img_y;
        int img_x = screen_x_start - x;

        uint32_t *buf_line = current_draw_buf->shm_data + (sy * WIDTH) + screen_x_start;
        const uint32_t *data_line = img_data + (src_y * w) + img_x;

        for (int px = 0; px < copy_width; px++) {
            uint32_t src = data_line[px];
            uint8_t a = (src >> 24) & 0xFF;

            if (a == 0) {
                // 完全透明 → 背景をそのまま
                continue;
            } else if (a == 255) {
                // 完全不透明 → そのまま上書き
                buf_line[px] = src;
            } else {
                // アルファブレンド
                uint32_t dst = buf_line[px];
                uint8_t sr = (src >> 16) & 0xFF;
                uint8_t sg = (src >>  8) & 0xFF;
                uint8_t sb =  src        & 0xFF;
                uint8_t dr = (dst >> 16) & 0xFF;
                uint8_t dg = (dst >>  8) & 0xFF;
                uint8_t db =  dst        & 0xFF;

                uint8_t or_ = (sr * a + dr * (255 - a)) / 255;
                uint8_t og  = (sg * a + dg * (255 - a)) / 255;
                uint8_t ob  = (sb * a + db * (255 - a)) / 255;

                buf_line[px] = (0xFF << 24) | (or_ << 16) | (og << 8) | ob;
            }
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
                printf("Loaded font: %s\n", font_paths[i]);
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

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>

#include "ui.h"
#include "app.h"

// 1. 入力ボックス（確定文字列 + IME未確定文字列の同時表示対応）
void ui_inputbox(int x, int y, int w, int h, const char *text, const char *preedit, int cursor_visible) {
    draw_rect(x, y, w, h, 0xFF282C34);
    draw_rect_outline(x, y, w, h, 0x0061AFEF);

    int font_size = 18;
    int text_x = x + 14;
    int text_y = y + (h - font_size) / 2 - 2;

    int text_width = 0;
    int has_text = (text && strlen(text) > 0);
    int has_preedit = (preedit && strlen(preedit) > 0);

    if (!has_text && !has_preedit) {
        draw_text(text_x, text_y, "Search apps...", -1, left, font_size, 0x005C6370);
    } else {
        if (has_text) {
            draw_text(text_x, text_y, text, -1, left, font_size, 0x00ABB2BF);

            const char *p = text;
            FT_Set_Pixel_Sizes(ft_face, 0, font_size);
            while (*p) {
                uint32_t cp = utf8_next_codepoint(&p);
                if (!cp) break;
                if (FT_Load_Char(ft_face, cp, FT_LOAD_DEFAULT) == 0) {
                    text_width += (ft_face->glyph->advance.x >> 6);
                }
            }
        }

        // IME未確定文字列（下線風・水色で表示）
        if (has_preedit) {
            int preedit_x = text_x + text_width;
            draw_text(preedit_x, text_y, preedit, -1, left, font_size, 0x0061AFEF);

            const char *p = preedit;
            int preedit_width = 0;
            FT_Set_Pixel_Sizes(ft_face, 0, font_size);
            while (*p) {
                uint32_t cp = utf8_next_codepoint(&p);
                if (!cp) break;
                if (FT_Load_Char(ft_face, cp, FT_LOAD_DEFAULT) == 0) {
                    preedit_width += (ft_face->glyph->advance.x >> 6);
                }
            }
            // 未確定文字列の下線
            draw_rect(preedit_x, text_y + font_size + 2, preedit_width, 2, 0x0061AFEF);
            text_width += preedit_width;
        }
    }

    if (cursor_visible) {
        int cursor_x = text_x + text_width + 2;
        int cursor_y = y + (h - font_size) / 2;
        for (int cy = cursor_y; cy < cursor_y + font_size; cy++) {
            draw_pixel(cursor_x, cy, 0x0061AFEF);
            draw_pixel(cursor_x + 1, cy, 0x0061AFEF);
        }
    }
}

// コマンド直接実行の案内
void run_command_ui(int x, int y, int w, int h, int item_height, const char *search_query) {
    draw_rect(x, y, w, item_height - 4, 0xFF2C313A);
    draw_rect(x, y, 4, item_height - 4, 0x00C3B779); // 黄インジケータ
    char run_msg[300];
    snprintf(run_msg, sizeof(run_msg), "Run: %s", search_query+1);
    draw_text(x + 16, y + 8, run_msg, -1, left, 16, 0x00C3B779);
}

// ウェブ検索の案内
void run_websearch_ui(int x, int y, int w, int h, int item_height, const char *search_query) {
    draw_rect(x, y, w, item_height - 4, 0xFF2C313A);
    draw_rect(x, y, 4, item_height - 4, 0x0098C379); // 緑インジケータ
    char run_msg[300];
    snprintf(run_msg, sizeof(run_msg), "Search: %s", search_query);
    draw_text(x + 16, y + 8, run_msg, -1, left, 16, 0x0098C379);
}


// 2. リストビュー（スクロール計算 & アイテム一覧 & スクロールバー描画）
void ui_app_list_view(int x, int y, int w, int h, int item_height,
                      int *scroll_offset, int selected, const char *search_query) {
    int max_display = h / item_height;

    if (search_query[0]=='>') {
        run_command_ui(x,y,w,h,item_height,search_query);
        return;
    }
    
    // 候補が0件の場合
    if (filtered_count == 0) {
        if (search_query && strlen(search_query) > 0) {
            run_websearch_ui(x,y,w,h,item_height,search_query);
        } else {
            draw_text(x + 16, y + 8, "No applications found", -1, left, 16, 0x005C6370);
        }
        return;
    }

    // スクロール追従
    if (selected < *scroll_offset) {
        *scroll_offset = selected;
    } else if (selected >= *scroll_offset + max_display) {
        *scroll_offset = selected - max_display + 1;
    }
    if (*scroll_offset < 0) *scroll_offset = 0;

    int display_count = filtered_count - *scroll_offset;
    if (display_count > max_display) display_count = max_display;

    int list_w = w - 10; // 右側にスクロールバー用の余白 10px を空ける

    for (int i = 0; i < display_count; i++) {
        int item_idx = *scroll_offset + i;
        int item_y = y + (i * item_height);
        int app_idx = filtered_indices[item_idx];
        int is_selected = (item_idx == selected);

        int w, h;
        uint32_t *pixels = load_image_stb_uint32(app_list[app_idx].icon_path, &w, &h);
        
        if (is_selected) {
            draw_rect(x, item_y, list_w, item_height - 4, 0xFF2C313A);
            draw_rect(x, item_y, 4, item_height - 4, 0x0061AFEF);
            draw_text(x + 64, item_y + 8, app_list[app_idx].name, -1, left, 16, 0x00FFFFFF);
            draw_image(x + 16, item_y + 4, w, h, pixels);
        } else {
            draw_text(x + 64, item_y + 8, app_list[app_idx].name, -1, left, 16, 0x00ABB2BF);
            draw_image(x + 16, item_y + 4, w, h, pixels);
        }
        free(pixels);
    }

    // スクロールバー描画（全体が画面内に収まらない場合のみ）
    if (filtered_count > max_display) {
        int bar_x = x + w - 4;
        int bar_y = y;
        int bar_h = max_display * item_height;

        // スクロールバー背景（薄い縦溝）
        draw_rect(bar_x, bar_y, 4, bar_h, 0xFF282C34);

        // つまみ（Thumb）の計算
        int thumb_h = (bar_h * max_display) / filtered_count;
        if (thumb_h < 12) thumb_h = 12;
        int max_scroll = filtered_count - max_display;
        int thumb_y = bar_y + ((*scroll_offset) * (bar_h - thumb_h)) / max_scroll;

        draw_rect(bar_x, thumb_y, 4, thumb_h, 0x005C6370);
    }
}

// 3. フッター
void ui_footer(int x, int y, const char *help_text) {
    draw_text(x, y, help_text, -1, left, 13, 0x005C6370);
}

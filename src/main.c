#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <errno.h>

#include "config.h"
#include "app.h"
#include "graphics.h"
#include "ui.h"
#include "wayland_backend.h"

int main()
{
    if (init_freetype() < 0)
    {
        fprintf(stderr, "Warning: FreeType font initialization failed\n");
    }

    if (init_wayland() < 0)
    {
        fprintf(stderr, "Failed to initialize Wayland\n");
        return 1;
    }

    // 初期表示用にアプリ一覧を読み込んでフィルタリング
    load_applications();
    update_filter(search_query);

    int display_fd = wl_display_get_fd(display);
    int frame_count = 0;

    int last_cursor_blink = -1;
    int last_selected_index = -1;
    int last_filtered_count = -1;
    char last_query[SEARCH_BUFFER_SIZE] = "";
    char last_preedit[SEARCH_BUFFER_SIZE] = "";

    while (1)
    {
        while (wl_display_prepare_read(display) != 0)
        {
            wl_display_dispatch_pending(display);
        }
        wl_display_flush(display);

        struct pollfd pfd = {
            .fd = display_fd,
            .events = POLLIN,
        };

        // 16ms 待機（約60FPSの周期）
        int ret = poll(&pfd, 1, FRAMETIME);

        if (ret > 0)
        {
            wl_display_read_events(display);
            wl_display_dispatch_pending(display);
        }
        else if (ret == 0)
        {
            wl_display_cancel_read(display);
        }
        else
        {
            wl_display_cancel_read(display);
            if (errno != EINTR)
                break;
        }

        if (!running) break;

        uint64_t now_ms = get_time_ms();
        int cursor_blink = (now_ms / 500) % 2 == 0;

        // 画面に変化があった場合のみレンダリング（無駄な毎フレーム再描画を抑止してCPU 0%へ）
        int needs_redraw = (cursor_blink != last_cursor_blink) ||
                           (selected_index != last_selected_index) ||
                           (filtered_count != last_filtered_count) ||
                           (strcmp(search_query, last_query) != 0) ||
                           (strcmp(preedit_string, last_preedit) != 0);

        if (needs_redraw && configured && begin_frame())
        {
            last_cursor_blink = cursor_blink;
            last_selected_index = selected_index;
            last_filtered_count = filtered_count;
            strncpy(last_query, search_query, sizeof(last_query) - 1);
            strncpy(last_preedit, preedit_string, sizeof(last_preedit) - 1);
            frame_count++;

            // 1. 画面クリア
            clear_screen(0xFF1E222A);

            // 2. ヘッダー（タイトル ＆ 件数インジケータ）
            draw_text(30, 18, "WAYLAUNCH", -1, left, 12, 0x005C6370);
            if (filtered_count > 0) {
                char count_str[32];
                snprintf(count_str, sizeof(count_str), "%d / %d", selected_index + 1, filtered_count);
                draw_text(WIDTH - 30, 18, count_str, -1, right, 12, 0x005C6370);
            }

            // 3. 検索ボックス (幅 540, 高さ 40, IME未確定文字列も同時表示)
            ui_inputbox(30, 36, WIDTH - 60, 40, search_query, preedit_string, cursor_blink);

            // 4. アプリリストビュー (y: 90 から 高さ 270 分の領域)
            ui_app_list_view(30, 90, WIDTH - 60, HEIGHT - 120, 38,
                             &scroll_offset, selected_index, search_query);

            // 5. フッターヘルプ
            ui_footer(30, HEIGHT - 20, "[Up/Down] Select  [Click] Launch  [Type] Search  [Enter] Launch  [Esc] Exit");

            // 6. 画面に反映
            end_frame();
        }
    }

    return 0;
}

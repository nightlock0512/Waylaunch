#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "wayland_backend.h"
#include "app.h"

struct wl_display *display = NULL;
struct wl_registry *registry = NULL;
struct wl_compositor *compositor = NULL;
struct xdg_wm_base *xdg_wm_base = NULL;
struct wl_surface *surface = NULL;
struct xdg_surface *xdg_surface = NULL;
struct xdg_toplevel *xdg_toplevel = NULL;
struct wl_shm *shm = NULL;
struct wl_seat *seat = NULL;
struct wl_keyboard *keyboard = NULL;
struct wl_pointer *pointer = NULL;
struct zwp_text_input_manager_v3 *text_input_manager = NULL;
struct zwp_text_input_v3 *text_input = NULL;

struct xkb_context *xkb_context = NULL;
struct xkb_keymap *xkb_keymap = NULL;
struct xkb_state *xkb_state = NULL;

// キーボード入力の状態（移動量など）
static int key_left = 0;
static int key_right = 0;
static int key_up = 0;
static int key_down = 0;
int running = 1;
int configured = 0;

// 検索文字列バッファ
char search_query[SEARCH_BUFFER_SIZE] = "";
int search_query_len = 0;

// IME の未確定（プリエディット）文字列
char preedit_string[SEARCH_BUFFER_SIZE] = "";

// マウス状態
int mouse_x = -1;
int mouse_y = -1;
int scroll_offset = 0;

struct buffer_info buffers[2];

// UTF-8 文字列から最後の1文字（マルチバイト対応）を削除
void search_query_backspace(void) {
    if (search_query_len <= 0) return;
    int i = search_query_len - 1;
    // UTF-8 の後続バイト (10xxxxxx = 0x80..0xBF) をスキップして先頭バイトを見つける
    while (i > 0 && ((unsigned char)search_query[i] & 0xC0) == 0x80) {
        i--;
    }
    search_query[i] = '\0';
    search_query_len = i;
}

// UTF-8 文字列を検索クエリに追加
void search_query_append(const char *text) {
    if (!text) return;
    int add_len = strlen(text);
    if (search_query_len + add_len < SEARCH_BUFFER_SIZE) {
        memcpy(search_query + search_query_len, text, add_len);
        search_query_len += add_len;
        search_query[search_query_len] = '\0';
        update_filter(search_query);
    }
}

static int create_shm_file(off_t size)
{
    int fd = memfd_create("shm", MFD_CLOEXEC);
    if (fd < 0)
        return -1;
    if (ftruncate(fd, size) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
    xdg_wm_base_pong(xdg_wm_base, serial);
}
static const struct xdg_wm_base_listener xdg_wm_base_listener = {.ping = xdg_wm_base_ping};

static void buffer_release(void *data, struct wl_buffer *wl_buffer)
{
    struct buffer_info *my_buf = (struct buffer_info *)data;
    my_buf->busy = 0;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

int create_buffer(struct buffer_info *b)
{
    int stride = WIDTH * 4;
    int size = stride * HEIGHT;
    int fd = create_shm_file(size);
    if (fd < 0)
        return -1;

    b->shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (b->shm_data == MAP_FAILED)
    {
        close(fd);
        return -1;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    b->buffer = wl_shm_pool_create_buffer(pool, 0, WIDTH, HEIGHT, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    b->busy = 0;
    wl_buffer_add_listener(b->buffer, &buffer_listener, b);
    return 0;
}

struct buffer_info *get_free_buffer(void)
{
    for (int i = 0; i < 2; i++)
    {
        if (!buffers[i].busy)
        {
            return &buffers[i];
        }
    }
    return NULL;
}

int begin_frame(void)
{
    current_draw_buf = get_free_buffer();
    if (!current_draw_buf)
        return 0;
    return 1;
}

void end_frame(void)
{
    if (!current_draw_buf)
        return;
    current_draw_buf->busy = 1;
    wl_surface_attach(surface, current_draw_buf->buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, WIDTH, HEIGHT);
    wl_surface_commit(surface);
    current_draw_buf = NULL;
}

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
    xdg_surface_ack_configure(xdg_surface, serial);
    configured = 1;
}
static const struct xdg_surface_listener xdg_surface_listener = {.configure = xdg_surface_configure};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states) {}
static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) { exit(0); }
static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close};

static void keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
                            uint32_t format, int32_t fd, uint32_t size) {
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    char *map_str = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map_str == MAP_FAILED) {
        close(fd);
        return;
    }

    if (xkb_keymap) xkb_keymap_unref(xkb_keymap);
    if (xkb_state) xkb_state_unref(xkb_state);

    xkb_keymap = xkb_keymap_new_from_string(xkb_context, map_str,
                                            XKB_KEYMAP_FORMAT_TEXT_V1,
                                            XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_str, size);
    close(fd);

    if (xkb_keymap) {
        xkb_state = xkb_state_new(xkb_keymap);
    }
}

void update_text_input_state(void) {
    if (!text_input) return;
    zwp_text_input_v3_set_surrounding_text(text_input, search_query, search_query_len, search_query_len);
    // 検索入力欄のカーソル位置 (x: 44, y: 36, w: 540, h: 40)
    zwp_text_input_v3_set_cursor_rectangle(text_input, 44, 36, 2, 40);
    zwp_text_input_v3_commit(text_input);
}

static void keyboard_enter(void *data, struct wl_keyboard *wl_keyboard,
                          uint32_t serial, struct wl_surface *surface,
                          struct wl_array *keys) {
    if (text_input) {
        zwp_text_input_v3_enable(text_input);
        update_text_input_state();
    }
}

static void keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
                          uint32_t serial, struct wl_surface *surface) {
    key_left = key_right = key_up = key_down = 0;
    if (text_input) {
        zwp_text_input_v3_disable(text_input);
        zwp_text_input_v3_commit(text_input);
    }
}

static void keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
                        uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    if (!xkb_state) return;

    // Wayland の key は evdev 準拠なので +8 して xkb_keycode にする
    xkb_keycode_t keycode = key + 8;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state, keycode);
    int is_press = (state == WL_KEYBOARD_KEY_STATE_PRESSED);

    if (is_press) {
        if (sym == XKB_KEY_Escape) {
            running = 0; // Esc キーで終了
        } else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
            // Enter キーでアプリまたは直接コマンドを起動
            if (filtered_count > 0 && selected_index >= 0 && selected_index < filtered_count) {
                int app_idx = filtered_indices[selected_index];
                launch_app(app_list[app_idx].exec);
            } else if (search_query_len > 0) {
                if (search_query[0] == '>')
                {
                    launch_app(search_query+1);
                }else{
                    char base_command[] = "open https://google.com/search?q=";
                    char *search_command = malloc(strlen(search_query) + strlen(base_command) + 1);
                    if (search_command != NULL) {
                        memcpy(search_command, base_command, strlen(base_command));
                        memcpy(search_command + strlen(base_command), search_query, strlen(search_query) + 1);
                        launch_app(search_command);
                        free(search_command);
                    }
                }
                
            }
            running = 0;
        } else if (sym == XKB_KEY_BackSpace) {
            search_query_backspace();
            update_filter(search_query);
            update_text_input_state();
        } else if (sym == XKB_KEY_Down) {
            if (filtered_count > 0) {
                selected_index = (selected_index + 1) % filtered_count;
            }
        } else if (sym == XKB_KEY_Up) {
            if (filtered_count > 0) {
                selected_index = (selected_index - 1 + filtered_count) % filtered_count;
            }
        } else {
            // 文字入力の取得（UTF-8: 日本語・記号・アクセント文字等のマルチバイトも許可）
            char buf[16];
            int size = xkb_state_key_get_utf8(xkb_state, keycode, buf, sizeof(buf));
            if (size > 0 && (unsigned char)buf[0] >= 32 && (unsigned char)buf[0] != 127) {
                buf[size] = '\0';
                search_query_append(buf);
                update_text_input_state();
            }
        }
    }
}

static void keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
                              uint32_t serial, uint32_t mods_depressed,
                              uint32_t mods_latched, uint32_t mods_locked,
                              uint32_t group) {
    if (xkb_state) {
        xkb_state_update_mask(xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
    }
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
                                int32_t rate, int32_t delay) {}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static void pointer_enter(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t sx, wl_fixed_t sy) {
    mouse_x = wl_fixed_to_int(sx);
    mouse_y = wl_fixed_to_int(sy);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface) {
    mouse_x = -1;
    mouse_y = -1;
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
                           uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
    mouse_x = wl_fixed_to_int(sx);
    mouse_y = wl_fixed_to_int(sy);

    // マウスホバーでアプリ項目を選択
    int list_y = 90;
    int item_h = 44;
    if (mouse_x >= 30 && mouse_x <= WIDTH - 30 && mouse_y >= list_y) {
        int index_in_view = (mouse_y - list_y) / item_h;
        int target = scroll_offset + index_in_view;
        if (target >= 0 && target < filtered_count) {
            selected_index = target;
        }
    }
}

static void pointer_button(void *data, struct wl_pointer *pointer,
                           uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    // 0x110 = BTN_LEFT (左クリック)
    if (button == 0x110 && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (filtered_count > 0 && selected_index >= 0 && selected_index < filtered_count) {
            int app_idx = filtered_indices[selected_index];
            launch_app(app_list[app_idx].exec);
        } else if (search_query_len > 0) {
            launch_app(search_query);
        }
        running = 0;
    }
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
                         uint32_t time, uint32_t axis, wl_fixed_t value) {
    // マウスホイールスクロール
    int delta = wl_fixed_to_int(value);
    if (delta > 0) {
        if (selected_index < filtered_count - 1) selected_index++;
    } else if (delta < 0) {
        if (selected_index > 0) selected_index--;
    }
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
};

/* =========================================================================
 *  IME (zwp_text_input_v3) リスナー
 * ========================================================================= */

static void text_input_enter(void *data, struct zwp_text_input_v3 *zwp_text_input_v3, struct wl_surface *surface) {}
static void text_input_leave(void *data, struct zwp_text_input_v3 *zwp_text_input_v3, struct wl_surface *surface) {}

static void text_input_preedit_string(void *data, struct zwp_text_input_v3 *zwp_text_input_v3,
                                     const char *text, int32_t cursor_begin, int32_t cursor_end) {
    if (text) {
        strncpy(preedit_string, text, sizeof(preedit_string) - 1);
        preedit_string[sizeof(preedit_string) - 1] = '\0';
    } else {
        preedit_string[0] = '\0';
    }
}

static void text_input_commit_string(void *data, struct zwp_text_input_v3 *zwp_text_input_v3, const char *text) {
    if (text) {
        search_query_append(text);
        preedit_string[0] = '\0';
    }
}

static void text_input_delete_surrounding_text(void *data, struct zwp_text_input_v3 *zwp_text_input_v3,
                                              uint32_t before_length, uint32_t after_length) {
    for (uint32_t i = 0; i < before_length; i++) {
        search_query_backspace();
    }
    update_filter(search_query);
}

static void text_input_done(void *data, struct zwp_text_input_v3 *zwp_text_input_v3, uint32_t serial) {}

static const struct zwp_text_input_v3_listener text_input_listener = {
    .enter = text_input_enter,
    .leave = text_input_leave,
    .preedit_string = text_input_preedit_string,
    .commit_string = text_input_commit_string,
    .delete_surrounding_text = text_input_delete_surrounding_text,
    .done = text_input_done,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard) {
        keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && keyboard) {
        wl_keyboard_destroy(keyboard);
        keyboard = NULL;
    }

    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !pointer) {
        pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(pointer, &pointer_listener, NULL);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && pointer) {
        wl_pointer_destroy(pointer);
        pointer = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version)
{
    // This line prints every protocol name to your terminal so we can debug
    printf("Found protocol: %s (v%d)\n", interface, version);

    if (strcmp(interface, "wl_compositor") == 0)
    {
        // Bind to version 3 or lower for maximum compatibility
        uint32_t bind_version = (version < 3) ? version : 3;
        compositor = wl_registry_bind(registry, id, &wl_compositor_interface, bind_version);
    }
    else if (strcmp(interface, "xdg_wm_base") == 0)
    {
        xdg_wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(xdg_wm_base, &xdg_wm_base_listener, NULL);
    }
    else if (strcmp(interface, "wl_shm") == 0)
    {
        shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    }
    else if (strcmp(interface, "wl_seat") == 0)
    {
        seat = wl_registry_bind(registry, id, &wl_seat_interface, 1);
        wl_seat_add_listener(seat, &seat_listener, NULL);
    }
    else if (strcmp(interface, "zwp_text_input_manager_v3") == 0)
    {
        text_input_manager = wl_registry_bind(registry, id, &zwp_text_input_manager_v3_interface, 1);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t id) {}
static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove};

int init_wayland(void) {
    xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_context)
    {
        fprintf(stderr, "Failed to create xkb context\n");
        return -1;
    }

    display = wl_display_connect(NULL);
    if (!display)
    {
        fprintf(stderr, "Cannot connect to display.\n");
        return -1;
    }

    registry = wl_display_get_registry(display);
    if (!registry)
    {
        fprintf(stderr, "Cannot get display registry.\n");
        return -1;
    }

    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);

    if (!compositor || !xdg_wm_base || !shm)
    {
        fprintf(stderr, "Missing critical Wayland protocols\n");
        return -1;
    }

    // ダブルバッファの初期化
    if (create_buffer(&buffers[0]) < 0 || create_buffer(&buffers[1]) < 0)
    {
        fprintf(stderr, "Failed to create shm buffers\n");
        return -1;
    }

    surface = wl_compositor_create_surface(compositor);
    xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);

    xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);
    xdg_toplevel_set_title(xdg_toplevel, "Waylaunch");

    // IME (Text Input) の有効化
    if (text_input_manager && seat) {
        text_input = zwp_text_input_manager_v3_get_text_input(text_input_manager, seat);
        zwp_text_input_v3_add_listener(text_input, &text_input_listener, NULL);
        zwp_text_input_v3_enable(text_input);
        zwp_text_input_v3_set_content_type(text_input,
                                           ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
                                           ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
        zwp_text_input_v3_commit(text_input);
    }

    // Commit to trigger the initial configuration event
    wl_surface_commit(surface);
    return 0;
}

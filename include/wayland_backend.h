#ifndef WAYLAND_BACKEND_H
#define WAYLAND_BACKEND_H

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "xdg-shell-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "config.h"
#include "graphics.h"

// Wayland オブジェクト
extern struct wl_display *display;
extern struct wl_registry *registry;
extern struct wl_compositor *compositor;
extern struct xdg_wm_base *xdg_wm_base;
extern struct wl_surface *surface;
extern struct xdg_surface *xdg_surface;
extern struct xdg_toplevel *xdg_toplevel;
extern struct wl_shm *shm;
extern struct wl_seat *seat;
extern struct wl_keyboard *keyboard;
extern struct wl_pointer *pointer;
extern struct zwp_text_input_manager_v3 *text_input_manager;
extern struct zwp_text_input_v3 *text_input;

extern struct xkb_context *xkb_context;
extern struct xkb_keymap *xkb_keymap;
extern struct xkb_state *xkb_state;

// アプリケーション状態
extern int running;
extern int configured;
extern char search_query[SEARCH_BUFFER_SIZE];
extern int search_query_len;
extern char preedit_string[SEARCH_BUFFER_SIZE];
extern int mouse_x;
extern int mouse_y;
extern int scroll_offset;

// バッファ管理
extern struct buffer_info buffers[2];

int init_wayland(void);
void cleanup_wayland(void);
int create_buffer(struct buffer_info *b);
struct buffer_info *get_free_buffer(void);
int begin_frame(void);
void end_frame(void);
void update_text_input_state(void);
void search_query_backspace(void);
void search_query_append(const char *text);

#endif // WAYLAND_BACKEND_H

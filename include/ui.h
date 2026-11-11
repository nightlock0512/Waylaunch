#ifndef UI_H
#define UI_H

#include "graphics.h"

void ui_inputbox(int x, int y, int w, int h, const char *text, const char *preedit, int cursor_visible);
void ui_app_list_view(int x, int y, int w, int h, int item_height, int *scroll_offset, int selected, const char *search_query);
void ui_footer(int x, int y, const char *help_text);

#endif // UI_H

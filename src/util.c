#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "app.h"
#include "config.h"
#include "util.h"

extern char search_query[];
extern int search_query_len;

int debug_enabled = 0;

void debug_log(const char *fmt, ...) {
    if (!debug_enabled) {
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

void search_query_backspace(void) {
    if (search_query_len <= 0) return;
    int i = search_query_len - 1;
    while (i > 0 && ((unsigned char)search_query[i] & 0xC0) == 0x80) {
        i--;
    }
    search_query[i] = '\0';
    search_query_len = i;
}

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

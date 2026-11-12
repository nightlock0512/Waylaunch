#ifndef UTIL_H
#define UTIL_H

#include <stdarg.h>

extern int debug_enabled;

void debug_log(const char *fmt, ...);
void search_query_backspace(void);
void search_query_append(const char *text);

#endif // UTIL_H

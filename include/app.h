#ifndef APP_H
#define APP_H

#include "config.h"

// アプリ情報構造体
struct app_entry {
    char name[128];
    char exec[256];
    char icon[256];
    char icon_path[512];
    int launch_count;
};

// アプリ一覧・フィルタリング情報
extern struct app_entry app_list[MAX_APPS];
extern int app_count;

extern int filtered_indices[MAX_APPS];
extern int filtered_count;
extern int selected_index;

void resolve_icon_path(struct app_entry *app);
void parse_desktop_file(const char *filepath);
void scan_applications_dir(const char *dirpath);
void load_applications(void);
void update_filter(const char *search_query);
void launch_app(const char *command);

#endif // APP_H

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/wait.h>
#include <dirent.h>

#include "app.h"

struct app_entry app_list[MAX_APPS];
int app_count = 0;

int filtered_indices[MAX_APPS];
int filtered_count = 0;
int selected_index = 0;

// アイコンファイル（PNG）のパスを解決する
void resolve_icon_path(struct app_entry *app) {
    if (app->icon[0] == '\0') return;

    // フルパスが直接指定されている場合
    if (app->icon[0] == '/') {
        if (access(app->icon, R_OK) == 0) {
            strncpy(app->icon_path, app->icon, sizeof(app->icon_path) - 1);
            return;
        }
    }

    const char *search_dirs[] = {
        "/usr/share/icons/hicolor/48x48/apps",
        "/usr/share/icons/hicolor/32x32/apps",
        "/usr/share/icons/hicolor/64x64/apps",
        "/usr/share/icons/hicolor/128x128/apps",
        "/usr/share/icons/hicolor/256x256/apps",
        "/usr/share/pixmaps",
        NULL
    };

    char candidate[512];
    for (int i = 0; search_dirs[i]; i++) {
        // .png なしで指定されている場合
        snprintf(candidate, sizeof(candidate), "%s/%s.png", search_dirs[i], app->icon);
        if (access(candidate, R_OK) == 0) {
            strncpy(app->icon_path, candidate, sizeof(app->icon_path) - 1);
            return;
        }
        // すでに .png が含まれている場合
        snprintf(candidate, sizeof(candidate), "%s/%s", search_dirs[i], app->icon);
        if (access(candidate, R_OK) == 0) {
            strncpy(app->icon_path, candidate, sizeof(app->icon_path) - 1);
            return;
        }
    }
}

// .desktop ファイルから Name と Exec を抽出
void parse_desktop_file(const char *filepath) {
    if (app_count >= MAX_APPS) return;

    FILE *f = fopen(filepath, "r");
    if (!f) return;

    char line[512];
    char name[128] = "";
    char exec[256] = "";
    char icon[256] = "";
    int in_entry_section = 0;
    int no_display = 0;

    while (fgets(line, sizeof(line), f)) {
        // 改行を削除
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '[') {
            if (strcmp(line, "[Desktop Entry]") == 0) {
                in_entry_section = 1;
            } else {
                in_entry_section = 0;
            }
            continue;
        }

        if (!in_entry_section) continue;

        if (strncmp(line, "NoDisplay=", 10) == 0 && strcmp(line + 10, "true") == 0) {
            no_display = 1;
            break;
        }

        if (name[0] == '\0' && strncmp(line, "Name=", 5) == 0) {
            strncpy(name, line + 5, sizeof(name) - 1);
        } else if (icon[0] == '\0' && strncmp(line, "Icon=", 5) == 0) {
            strncpy(icon, line + 5, sizeof(icon) - 1);
        } else if (exec[0] == '\0' && strncmp(line, "Exec=", 5) == 0) {
            // Exec= のフィールドコード (%u, %f 等) を除去
            const char *src = line + 5;
            char *dst = exec;
            while (*src && (dst - exec) < (int)sizeof(exec) - 1) {
                if (*src == '%' && *(src + 1) != '\0') {
                    src += 2; // %u, %f などをスキップ
                } else {
                    *dst++ = *src++;
                }
            }
            *dst = '\0';
            // 末尾の空白を除去
            while (dst > exec && isspace((unsigned char)*(dst - 1))) {
                *(--dst) = '\0';
            }
        }
    }
    fclose(f);

    if (no_display || name[0] == '\0' || exec[0] == '\0') {
        return;
    }

    // 重複登録防止（同じ名前のアプリが既にある場合はスキップ）
    for (int i = 0; i < app_count; i++) {
        if (strcmp(app_list[i].name, name) == 0) return;
    }

    strncpy(app_list[app_count].name, name, sizeof(app_list[app_count].name) - 1);
    strncpy(app_list[app_count].exec, exec, sizeof(app_list[app_count].exec) - 1);
    app_count++;
}

// 指定ディレクトリの .desktop ファイルを走査
void scan_applications_dir(const char *dirpath) {
    DIR *dir = opendir(dirpath);
    if (!dir) return;

    struct dirent *entry;
    char filepath[1024];

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG || entry->d_type == DT_LNK || entry->d_type == DT_UNKNOWN) {
            int len = strlen(entry->d_name);
            if (len > 8 && strcmp(entry->d_name + len - 8, ".desktop") == 0) {
                snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, entry->d_name);
                parse_desktop_file(filepath);
            }
        }
    }
    closedir(dir);
}

// システム全体のアプリを読み込む
void load_applications(void) {
    app_count = 0;
    // ユーザーローカル
    const char *home = getenv("HOME");
    if (home) {
        char user_apps[512];
        snprintf(user_apps, sizeof(user_apps), "%s/.local/share/applications", home);
        scan_applications_dir(user_apps);
    }
    // システム共通
    scan_applications_dir("/usr/share/applications");
    scan_applications_dir("/usr/local/share/applications");

    printf("Loaded %d applications from system.\n", app_count);
}

// 大文字小文字を区別しない部分一致判定
static int str_contains_case_insensitive(const char *haystack, const char *needle) {
    if (!needle || needle[0] == '\0') return 1;
    if (!haystack) return 0;

    int n_len = strlen(needle);
    int h_len = strlen(haystack);
    if (n_len > h_len) return 0;

    for (int i = 0; i <= h_len - n_len; i++) {
        int match = 1;
        for (int j = 0; j < n_len; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

// 検索クエリに基づいてフィルタを更新
void update_filter(const char *search_query) {
    filtered_count = 0;
    for (int i = 0; i < app_count; i++) {
        if (str_contains_case_insensitive(app_list[i].name, search_query)) {
            filtered_indices[filtered_count++] = i;
        }
    }
    selected_index = 0; // 絞り込みが変わったら一番上を選択
}

// アプリをバックグラウンドで起動（親プロセス・端末から完全に分離）
void launch_app(const char *command) {
    if (!command || strlen(command) == 0) return;
    pid_t pid = fork();
    if (pid == 0) {
        // 新しいセッションを作成
        setsid();

        // 孫プロセスを作成して親(ランチャー)のゾンビ化・待機依存を完全に断つ
        pid_t pid2 = fork();
        if (pid2 > 0) {
            _exit(0); // 中間プロセスは即時終了
        }

        // 標準入出力をすべて /dev/null にリダイレクトして端末から切り離す
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            if (null_fd > 2) close(null_fd);
        }

        // 3番以降の開いているファイルディスクリプタを閉じる
        for (int fd = 3; fd < 256; fd++) {
            close(fd);
        }

        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }
    
    // 中間プロセスの回収
    waitpid(pid, NULL, 0);
}

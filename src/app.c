#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/wait.h>
#include <dirent.h>
#include <ftw.h>

#include "app.h"

struct app_entry app_list[MAX_APPS];
int app_count = 0;

int filtered_indices[MAX_APPS];
int filtered_count = 0;
int selected_index = 0;

static char target_icon_name[256];
static char found_path[512];

// nftw からコールバックされる判定関数
static int icon_search_callback(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
    (void)sb; (void)ftwbuf; // 未使用警告の回避

    // 通常のファイルのみチェック
    if (tflag == FTW_F) {
        const char *filename = strrchr(fpath, '/');
        filename = (filename) ? filename + 1 : fpath;

        // 1. 完全一致（例: icon が "firefox.png" の場合）
        if (strcmp(filename, target_icon_name) == 0) {
            strncpy(found_path, fpath, sizeof(found_path) - 1);
            return 1; // 検索中断（発見）
        }

        // 2. 拡張子補完一致（例: icon が "firefox" で "firefox.png", "firefox.svg" など）
        size_t name_len = strlen(target_icon_name);
        if (strncmp(filename, target_icon_name, name_len) == 0 && filename[name_len] == '.') {
            const char *ext = filename + name_len + 1;
            if (strcmp(ext, "png") == 0 || strcmp(ext, "svg") == 0 || strcmp(ext, "xpm") == 0) {
                strncpy(found_path, fpath, sizeof(found_path) - 1);
                return 1; // 検索中断（発見）
            }
        }
    }
    return 0; // 検索継続
}

void resolve_icon_path(struct app_entry *app) {
    if (app->icon[0] == '\0') return;

    // 1. フルパス指定の場合
    if (app->icon[0] == '/') {
        if (access(app->icon, R_OK) == 0) {
            strncpy(app->icon_path, app->icon, sizeof(app->icon_path) - 1);
            app->icon_path[sizeof(app->icon_path) - 1] = '\0';
            return;
        }
    }

    // 2. XDG 仕様に従い検索ベースディレクトリを構築
    //    $XDG_DATA_HOME (デフォルト: $HOME/.local/share) と
    //    $XDG_DATA_DIRS (デフォルト: /usr/local/share:/usr/share) から
    //    それぞれ /icons と /pixmaps を検索対象にする

    // --- XDG_DATA_HOME ---
    char data_home[512];
    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdg_data_home && xdg_data_home[0]) {
        strncpy(data_home, xdg_data_home, sizeof(data_home) - 1);
        data_home[sizeof(data_home) - 1] = '\0';
    } else if (home && home[0]) {
        snprintf(data_home, sizeof(data_home), "%s/.local/share", home);
    } else {
        data_home[0] = '\0';
    }

    // --- XDG_DATA_DIRS ---
    const char *xdg_data_dirs = getenv("XDG_DATA_DIRS");
    char dirs_buf[2048];
    if (xdg_data_dirs && xdg_data_dirs[0]) {
        strncpy(dirs_buf, xdg_data_dirs, sizeof(dirs_buf) - 1);
        dirs_buf[sizeof(dirs_buf) - 1] = '\0';
    } else {
        strncpy(dirs_buf, "/usr/local/share:/usr/share", sizeof(dirs_buf) - 1);
        dirs_buf[sizeof(dirs_buf) - 1] = '\0';
    }

    // 検索ディレクトリを動的に構築（各 data dir に /icons と /pixmaps を付加）
    #define MAX_SEARCH_DIRS 64
    const char *search_dirs[MAX_SEARCH_DIRS];
    static char search_paths[MAX_SEARCH_DIRS][512]; // static で寿命を保証
    int dir_count = 0;

    // data_home/icons, data_home/pixmaps を最優先で追加
    if (data_home[0]) {
        snprintf(search_paths[dir_count], sizeof(search_paths[dir_count]),
                 "%s/icons", data_home);
        search_dirs[dir_count] = search_paths[dir_count];
        dir_count++;

        snprintf(search_paths[dir_count], sizeof(search_paths[dir_count]),
                 "%s/pixmaps", data_home);
        search_dirs[dir_count] = search_paths[dir_count];
        dir_count++;
    }

    // XDG_DATA_DIRS の各エントリに /icons と /pixmaps を追加
    char *saveptr = NULL;
    char *tok = strtok_r(dirs_buf, ":", &saveptr);
    while (tok && dir_count < MAX_SEARCH_DIRS - 2) {
        // 末尾の / を除去
        size_t tlen = strlen(tok);
        while (tlen > 1 && tok[tlen - 1] == '/') tok[--tlen] = '\0';

        snprintf(search_paths[dir_count], sizeof(search_paths[dir_count]),
                 "%s/icons", tok);
        search_dirs[dir_count] = search_paths[dir_count];
        dir_count++;

        snprintf(search_paths[dir_count], sizeof(search_paths[dir_count]),
                 "%s/pixmaps", tok);
        search_dirs[dir_count] = search_paths[dir_count];
        dir_count++;

        tok = strtok_r(NULL, ":", &saveptr);
    }
    search_dirs[dir_count] = NULL;

    // 検索状態の初期化
    strncpy(target_icon_name, app->icon, sizeof(target_icon_name) - 1);
    target_icon_name[sizeof(target_icon_name) - 1] = '\0';
    found_path[0] = '\0';

    // 3. 各ディレクトリ以下を再帰的に走査
    for (int i = 0; search_dirs[i] != NULL; i++) {
        if (access(search_dirs[i], R_OK) != 0) continue;

        // nftw: ディレクトリ以下を再帰探索
        if (nftw(search_dirs[i], icon_search_callback, 20, FTW_PHYS) == 1) {
            // ヒットした場合
            strncpy(app->icon_path, found_path, sizeof(app->icon_path) - 1);
            app->icon_path[sizeof(app->icon_path) - 1] = '\0';
            return;
        }
    }

    // 見つからなかった場合
    app->icon_path[0] = '\0';
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
    strncpy(app_list[app_count].icon, icon, sizeof(app_list[app_count].icon) - 1);
    resolve_icon_path(&app_list[app_count]);
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

// システム全体のアプリを読み込む（XDG_DATA_HOME / XDG_DATA_DIRS に準拠）
void load_applications(void) {
    app_count = 0;

    // XDG_DATA_HOME
    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    char data_home[512];
    if (xdg_data_home && xdg_data_home[0]) {
        strncpy(data_home, xdg_data_home, sizeof(data_home) - 1);
        data_home[sizeof(data_home) - 1] = '\0';
    } else if (home && home[0]) {
        snprintf(data_home, sizeof(data_home), "%s/.local/share", home);
    } else {
        data_home[0] = '\0';
    }

    if (data_home[0]) {
        char user_apps[512];
        snprintf(user_apps, sizeof(user_apps), "%s/applications", data_home);
        scan_applications_dir(user_apps);
    }

    // XDG_DATA_DIRS
    const char *xdg_data_dirs = getenv("XDG_DATA_DIRS");
    char dirs_buf[2048];
    if (xdg_data_dirs && xdg_data_dirs[0]) {
        strncpy(dirs_buf, xdg_data_dirs, sizeof(dirs_buf) - 1);
        dirs_buf[sizeof(dirs_buf) - 1] = '\0';
    } else {
        strncpy(dirs_buf, "/usr/local/share:/usr/share", sizeof(dirs_buf) - 1);
        dirs_buf[sizeof(dirs_buf) - 1] = '\0';
    }

    char *saveptr = NULL;
    char *tok = strtok_r(dirs_buf, ":", &saveptr);
    while (tok) {
        size_t tlen = strlen(tok);
        while (tlen > 1 && tok[tlen - 1] == '/') tok[--tlen] = '\0';

        char app_dir[512];
        snprintf(app_dir, sizeof(app_dir), "%s/applications", tok);
        scan_applications_dir(app_dir);
        tok = strtok_r(NULL, ":", &saveptr);
    }

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

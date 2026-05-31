#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#define APP_NAME L"BaiduRenamer"
#define DEFAULT_DOWNLOAD_DIR L"J:\\BaiduNetdiskDownload"
#define DEFAULT_BANDIZIP L"C:\\Program Files\\Bandizip\\Bandizip.exe"
#define DEFAULT_PASSWORD L"himengzhan.vip"

#define IDC_TAB 100
#define IDC_STATUS 101
#define IDC_LOG 102
#define IDC_CLEAR_LOG 103

#define IDC_EXT_FOLDER 200
#define IDC_EXT_BROWSE 201
#define IDC_EXT_RECURSIVE 202
#define IDC_EXT_START 203

#define IDC_EXTRACT_FOLDER 300
#define IDC_EXTRACT_BROWSE 301
#define IDC_EXTRACT_RECURSIVE 302
#define IDC_BANDIZIP 303
#define IDC_BANDIZIP_BROWSE 304
#define IDC_PASSWORD 305
#define IDC_EXTRACT_START 306

#define WM_APP_LOG (WM_APP + 1)
#define WM_APP_DONE (WM_APP + 2)

typedef struct Logger Logger;
typedef void (*LogCallback)(Logger *logger, const wchar_t *message);

struct Logger {
    LogCallback callback;
    void *ctx;
};

typedef struct {
    size_t scanned;
    size_t changed;
    size_t skipped;
    size_t failed;
    wchar_t **error_files;
    size_t error_count;
    size_t error_cap;
} RenameStats;

typedef struct {
    size_t first_targets;
    size_t first_success;
    size_t first_failed;
    size_t second_targets;
    size_t second_success;
    size_t second_failed;
    wchar_t **error_files;
    size_t error_count;
    size_t error_cap;
} ExtractStats;

typedef struct {
    wchar_t **items;
    size_t count;
    size_t cap;
} PathList;

typedef struct {
    wchar_t *path;
    wchar_t *name;
    DWORD attrs;
} DirEntry;

typedef struct {
    DirEntry *items;
    size_t count;
    size_t cap;
} DirEntryList;

typedef enum {
    TASK_EXTENSION = 1,
    TASK_EXTRACT = 2
} TaskKind;

typedef struct {
    HWND hwnd;
    HWND tab;
    HWND status;
    HWND title;
    HWND log_group;
    HWND log_edit;
    HWND clear_log;

    HWND ext_label_folder;
    HWND ext_folder;
    HWND ext_browse;
    HWND ext_recursive;
    HWND ext_start;

    HWND extract_label_folder;
    HWND extract_folder;
    HWND extract_browse;
    HWND extract_recursive;
    HWND label_bandizip;
    HWND bandizip;
    HWND bandizip_browse;
    HWND label_password;
    HWND password;
    HWND extract_start;

    HFONT font;
    HFONT title_font;
    int active_tab;
} AppState;

typedef struct {
    AppState *app;
    TaskKind task;
    wchar_t *folder;
    int recursive;
    wchar_t *bandizip;
    wchar_t *password;
} WorkerArgs;

static void *xmalloc(size_t size) {
    void *ptr = malloc(size ? size : 1);
    if (!ptr) {
        MessageBoxW(NULL, L"메모리가 부족합니다.", APP_NAME, MB_ICONERROR | MB_OK);
        ExitProcess(2);
    }
    return ptr;
}

static void *xrealloc(void *ptr, size_t size) {
    void *next = realloc(ptr, size ? size : 1);
    if (!next) {
        MessageBoxW(NULL, L"메모리가 부족합니다.", APP_NAME, MB_ICONERROR | MB_OK);
        ExitProcess(2);
    }
    return next;
}

static wchar_t *xwcsdup(const wchar_t *text) {
    if (!text) {
        text = L"";
    }
    size_t len = wcslen(text);
    wchar_t *copy = (wchar_t *)xmalloc((len + 1) * sizeof(wchar_t));
    memcpy(copy, text, (len + 1) * sizeof(wchar_t));
    return copy;
}

static int ci_equal_n(const wchar_t *a, const wchar_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        wchar_t ca = towlower(a[i]);
        wchar_t cb = towlower(b[i]);
        if (ca != cb) {
            return 0;
        }
        if (ca == L'\0') {
            return 1;
        }
    }
    return 1;
}

static int ci_equal(const wchar_t *a, const wchar_t *b) {
    if (!a || !b) {
        return a == b;
    }
    while (*a && *b) {
        if (towlower(*a) != towlower(*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static int ci_starts_with(const wchar_t *text, const wchar_t *prefix) {
    while (*prefix) {
        if (towlower(*text) != towlower(*prefix)) {
            return 0;
        }
        text++;
        prefix++;
    }
    return 1;
}

static void path_list_init(PathList *list) {
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static void path_list_append_owned(PathList *list, wchar_t *path) {
    if (list->count == list->cap) {
        size_t next_cap = list->cap ? list->cap * 2 : 16;
        list->items = (wchar_t **)xrealloc(list->items, next_cap * sizeof(wchar_t *));
        list->cap = next_cap;
    }
    list->items[list->count++] = path;
}

static void path_list_append_dup(PathList *list, const wchar_t *path) {
    path_list_append_owned(list, xwcsdup(path));
}

static void path_list_free(PathList *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static int compare_wstring_ptr_ci(const void *left, const void *right) {
    const wchar_t *a = *(const wchar_t * const *)left;
    const wchar_t *b = *(const wchar_t * const *)right;
    return _wcsicmp(a, b);
}

static void stats_add_error(wchar_t ***items, size_t *count, size_t *cap, const wchar_t *file_name) {
    if (*count == *cap) {
        size_t next_cap = *cap ? *cap * 2 : 8;
        *items = (wchar_t **)xrealloc(*items, next_cap * sizeof(wchar_t *));
        *cap = next_cap;
    }
    (*items)[(*count)++] = xwcsdup(file_name);
}

static void rename_stats_free(RenameStats *stats) {
    for (size_t i = 0; i < stats->error_count; i++) {
        free(stats->error_files[i]);
    }
    free(stats->error_files);
}

static void extract_stats_free(ExtractStats *stats) {
    for (size_t i = 0; i < stats->error_count; i++) {
        free(stats->error_files[i]);
    }
    free(stats->error_files);
}

static void log_text(Logger *logger, const wchar_t *message) {
    if (logger && logger->callback) {
        logger->callback(logger, message);
    }
}

static void log_format(Logger *logger, const wchar_t *fmt, ...) {
    wchar_t stack_buffer[8192];
    va_list args;
    va_start(args, fmt);
    int written = _vsnwprintf(stack_buffer, sizeof(stack_buffer) / sizeof(stack_buffer[0]) - 1, fmt, args);
    va_end(args);
    stack_buffer[(sizeof(stack_buffer) / sizeof(stack_buffer[0])) - 1] = L'\0';
    if (written < 0) {
        wcscpy(stack_buffer + (sizeof(stack_buffer) / sizeof(stack_buffer[0])) - 32, L"...(로그가 너무 깁니다)");
    }
    log_text(logger, stack_buffer);
}

static const wchar_t *last_separator(const wchar_t *path) {
    const wchar_t *slash = wcsrchr(path, L'\\');
    const wchar_t *forward = wcsrchr(path, L'/');
    if (!slash) {
        return forward;
    }
    if (!forward) {
        return slash;
    }
    return slash > forward ? slash : forward;
}

static const wchar_t *file_name_part(const wchar_t *path) {
    const wchar_t *sep = last_separator(path);
    return sep ? sep + 1 : path;
}

static wchar_t *file_name_dup(const wchar_t *path) {
    return xwcsdup(file_name_part(path));
}

static wchar_t *path_join(const wchar_t *dir, const wchar_t *name) {
    size_t dir_len = wcslen(dir);
    size_t name_len = wcslen(name);
    int needs_sep = dir_len > 0 && dir[dir_len - 1] != L'\\' && dir[dir_len - 1] != L'/';
    wchar_t *joined = (wchar_t *)xmalloc((dir_len + needs_sep + name_len + 1) * sizeof(wchar_t));
    wcscpy(joined, dir);
    if (needs_sep) {
        joined[dir_len] = L'\\';
        joined[dir_len + 1] = L'\0';
    }
    wcscat(joined, name);
    return joined;
}

static const wchar_t *last_suffix_dot(const wchar_t *path) {
    const wchar_t *name = file_name_part(path);
    size_t len = wcslen(name);
    if (len == 0) {
        return NULL;
    }
    for (const wchar_t *p = name + len - 1; p >= name; p--) {
        if (*p == L'.') {
            if (p != name && p[1] != L'\0') {
                return p;
            }
        }
        if (p == name) {
            break;
        }
    }
    return NULL;
}

static const wchar_t *previous_suffix_dot(const wchar_t *path, const wchar_t *last_dot) {
    const wchar_t *name = file_name_part(path);
    if (!last_dot || last_dot <= name) {
        return NULL;
    }
    for (const wchar_t *p = last_dot - 1; p >= name; p--) {
        if (*p == L'.') {
            if (p != name && p + 1 < last_dot) {
                return p;
            }
        }
        if (p == name) {
            break;
        }
    }
    return NULL;
}

static wchar_t *suffix_dup(const wchar_t *path) {
    const wchar_t *dot = last_suffix_dot(path);
    return dot ? xwcsdup(dot) : xwcsdup(L"");
}

static wchar_t *path_with_suffix(const wchar_t *path, const wchar_t *suffix) {
    const wchar_t *dot = last_suffix_dot(path);
    size_t prefix_len = dot ? (size_t)(dot - path) : wcslen(path);
    size_t suffix_len = wcslen(suffix);
    wchar_t *target = (wchar_t *)xmalloc((prefix_len + suffix_len + 1) * sizeof(wchar_t));
    memcpy(target, path, prefix_len * sizeof(wchar_t));
    memcpy(target + prefix_len, suffix, (suffix_len + 1) * sizeof(wchar_t));
    return target;
}

static wchar_t *path_remove_suffix(const wchar_t *path) {
    const wchar_t *dot = last_suffix_dot(path);
    if (!dot) {
        return xwcsdup(path);
    }
    size_t len = (size_t)(dot - path);
    wchar_t *target = (wchar_t *)xmalloc((len + 1) * sizeof(wchar_t));
    memcpy(target, path, len * sizeof(wchar_t));
    target[len] = L'\0';
    return target;
}

static wchar_t *parent_path_dup(const wchar_t *path) {
    const wchar_t *sep = last_separator(path);
    if (!sep) {
        return xwcsdup(L".");
    }
    if (sep == path) {
        return xwcsdup(L"\\");
    }
    if (sep > path && sep[-1] == L':') {
        size_t len = (size_t)(sep - path + 1);
        wchar_t *parent = (wchar_t *)xmalloc((len + 1) * sizeof(wchar_t));
        memcpy(parent, path, len * sizeof(wchar_t));
        parent[len] = L'\0';
        return parent;
    }
    size_t len = (size_t)(sep - path);
    wchar_t *parent = (wchar_t *)xmalloc((len + 1) * sizeof(wchar_t));
    memcpy(parent, path, len * sizeof(wchar_t));
    parent[len] = L'\0';
    return parent;
}

static wchar_t *trim_path_input(const wchar_t *input) {
    const wchar_t *start = input ? input : L"";
    while (*start && iswspace(*start)) {
        start++;
    }
    const wchar_t *end = start + wcslen(start);
    while (end > start && iswspace(end[-1])) {
        end--;
    }
    while (end > start && *start == L'"') {
        start++;
    }
    while (end > start && end[-1] == L'"') {
        end--;
    }
    size_t len = (size_t)(end - start);
    wchar_t *trimmed = (wchar_t *)xmalloc((len + 1) * sizeof(wchar_t));
    memcpy(trimmed, start, len * sizeof(wchar_t));
    trimmed[len] = L'\0';
    return trimmed;
}

static wchar_t *full_path_dup(const wchar_t *path) {
    DWORD len = GetFullPathNameW(path, 0, NULL, NULL);
    if (len == 0) {
        return xwcsdup(path);
    }
    wchar_t *full = (wchar_t *)xmalloc((len + 1) * sizeof(wchar_t));
    DWORD written = GetFullPathNameW(path, len + 1, full, NULL);
    if (written == 0 || written > len) {
        free(full);
        return xwcsdup(path);
    }
    return full;
}

static int same_normalized_path(const wchar_t *a, const wchar_t *b) {
    wchar_t *fa = full_path_dup(a);
    wchar_t *fb = full_path_dup(b);
    int same = _wcsicmp(fa, fb) == 0;
    free(fa);
    free(fb);
    return same;
}

static int path_exists(const wchar_t *path) {
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static int path_is_dir(const wchar_t *path) {
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static int path_is_file(const wchar_t *path) {
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static int ensure_directory(const wchar_t *path) {
    if (path_is_dir(path)) {
        return 1;
    }
    wchar_t *parent = parent_path_dup(path);
    if (!same_normalized_path(parent, path) && !path_is_dir(parent)) {
        if (!ensure_directory(parent)) {
            free(parent);
            return 0;
        }
    }
    free(parent);
    if (CreateDirectoryW(path, NULL)) {
        return 1;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS && path_is_dir(path);
}

static wchar_t *last_error_message_dup(DWORD error) {
    wchar_t *buffer = NULL;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = FormatMessageW(flags, NULL, error, 0, (LPWSTR)&buffer, 0, NULL);
    if (len == 0 || !buffer) {
        wchar_t fallback[64];
        swprintf(fallback, 64, L"Windows 오류 %lu", error);
        return xwcsdup(fallback);
    }
    while (len > 0 && (buffer[len - 1] == L'\r' || buffer[len - 1] == L'\n' || iswspace(buffer[len - 1]))) {
        buffer[--len] = L'\0';
    }
    wchar_t *copy = xwcsdup(buffer);
    LocalFree(buffer);
    return copy;
}

static int has_exe_suffix(const wchar_t *path) {
    const wchar_t *name = file_name_part(path);
    size_t len = wcslen(name);
    for (size_t i = 0; i < len; i++) {
        if (name[i] != L'.' || i == 0 || i + 1 >= len) {
            continue;
        }
        size_t start = i + 1;
        size_t end = start;
        while (end < len && name[end] != L'.') {
            end++;
        }
        if (end - start == 3 && ci_equal_n(name + start, L"exe", 3)) {
            return 1;
        }
    }
    return 0;
}

static int parse_month_01_12(const wchar_t *text) {
    if (!iswdigit(text[0]) || !iswdigit(text[1])) {
        return 0;
    }
    int month = (text[0] - L'0') * 10 + (text[1] - L'0');
    return month >= 1 && month <= 12;
}

static int is_exact_date_mp4_name(const wchar_t *path) {
    const wchar_t *name = file_name_part(path);
    size_t len = wcslen(name);
    if (len != 9 && len != 11) {
        return 0;
    }
    if (!ci_equal(name + len - 4, L".mp4")) {
        return 0;
    }
    size_t stem_len = len - 4;
    if (stem_len == 5) {
        return iswdigit(name[0]) && iswdigit(name[1]) &&
               name[2] == L'.' && parse_month_01_12(name + 3);
    }
    return iswdigit(name[0]) && iswdigit(name[1]) && iswdigit(name[2]) && iswdigit(name[3]) &&
           name[4] == L'.' && parse_month_01_12(name + 5);
}

static int get_front_ext_first_char(const wchar_t *path, wchar_t *first_char) {
    const wchar_t *last_dot = last_suffix_dot(path);
    const wchar_t *front_dot = previous_suffix_dot(path, last_dot);
    if (!front_dot || !front_dot[1]) {
        return 0;
    }
    *first_char = towlower(front_dot[1]);
    return 1;
}

static int suffix_first_char(const wchar_t *path, wchar_t *first_char) {
    const wchar_t *dot = last_suffix_dot(path);
    if (!dot || !dot[1]) {
        return 0;
    }
    *first_char = towlower(dot[1]);
    return 1;
}

static int extension_change_target(const wchar_t *path, wchar_t **target_out, const wchar_t **reason_out) {
    *target_out = NULL;
    *reason_out = L"";

    if (has_exe_suffix(path)) {
        *reason_out = L"exe 파일";
        return 0;
    }

    if (is_exact_date_mp4_name(path)) {
        *target_out = path_with_suffix(path, L".zip");
        *reason_out = L"YY.MM.mp4/YYYY.MM.mp4 상위 규칙";
        return 1;
    }

    wchar_t front_first = 0;
    if (get_front_ext_first_char(path, &front_first)) {
        if (front_first == L'7' || front_first == L'r') {
            *target_out = path_remove_suffix(path);
            *reason_out = L"이중확장자 뒤 확장자 제거";
            return 1;
        }
        if (front_first == L'0') {
            *reason_out = L"0으로 시작하는 이중확장자";
            return 0;
        }
    }

    if (!last_suffix_dot(path)) {
        size_t len = wcslen(path);
        wchar_t *target = (wchar_t *)xmalloc((len + 5) * sizeof(wchar_t));
        wcscpy(target, path);
        wcscat(target, L".zip");
        *target_out = target;
        *reason_out = L"zip 확장자 추가";
        return 1;
    }

    wchar_t last_first = 0;
    if (suffix_first_char(path, &last_first)) {
        if (last_first == L'0') {
            *reason_out = L"0으로 시작하는 확장자";
            return 0;
        }
        if (last_first == L'7') {
            *target_out = path_with_suffix(path, L".7z");
            *reason_out = L"7z로 변경";
            return 1;
        }
        if (last_first == L'r') {
            *target_out = path_with_suffix(path, L".rar");
            *reason_out = L"rar로 변경";
            return 1;
        }
    }

    *target_out = path_with_suffix(path, L".zip");
    *reason_out = L"zip으로 변경";
    return 1;
}

static void dir_entry_list_init(DirEntryList *list) {
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static void dir_entry_list_append(DirEntryList *list, wchar_t *path, wchar_t *name, DWORD attrs) {
    if (list->count == list->cap) {
        size_t next_cap = list->cap ? list->cap * 2 : 32;
        list->items = (DirEntry *)xrealloc(list->items, next_cap * sizeof(DirEntry));
        list->cap = next_cap;
    }
    list->items[list->count].path = path;
    list->items[list->count].name = name;
    list->items[list->count].attrs = attrs;
    list->count++;
}

static void dir_entry_list_free(DirEntryList *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].path);
        free(list->items[i].name);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static int compare_dir_entry_name_ci(const void *left, const void *right) {
    const DirEntry *a = (const DirEntry *)left;
    const DirEntry *b = (const DirEntry *)right;
    return _wcsicmp(a->name, b->name);
}

static int list_directory(const wchar_t *root, DirEntryList *entries, Logger *logger) {
    dir_entry_list_init(entries);
    wchar_t *pattern = path_join(root, L"*");
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    free(pattern);
    if (find == INVALID_HANDLE_VALUE) {
        wchar_t *err = last_error_message_dup(GetLastError());
        log_format(logger, L"폴더 접근 실패: %ls (%ls)", root, err);
        free(err);
        return 0;
    }

    do {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
            continue;
        }
        wchar_t *full = path_join(root, data.cFileName);
        dir_entry_list_append(entries, full, xwcsdup(data.cFileName), data.dwFileAttributes);
    } while (FindNextFileW(find, &data));

    FindClose(find);
    qsort(entries->items, entries->count, sizeof(DirEntry), compare_dir_entry_name_ci);
    return 1;
}

static void collect_files_recursive(const wchar_t *root, int recursive, PathList *files, Logger *logger) {
    DirEntryList entries;
    if (!list_directory(root, &entries, logger)) {
        return;
    }

    for (size_t i = 0; i < entries.count; i++) {
        DWORD attrs = entries.items[i].attrs;
        if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
            if (recursive && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
                collect_files_recursive(entries.items[i].path, recursive, files, logger);
            }
        } else {
            path_list_append_dup(files, entries.items[i].path);
        }
    }

    dir_entry_list_free(&entries);
}

static void collect_directories_recursive(const wchar_t *root, PathList *dirs, Logger *logger) {
    path_list_append_dup(dirs, root);

    DirEntryList entries;
    if (!list_directory(root, &entries, logger)) {
        return;
    }

    for (size_t i = 0; i < entries.count; i++) {
        DWORD attrs = entries.items[i].attrs;
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
            collect_directories_recursive(entries.items[i].path, dirs, logger);
        }
    }

    dir_entry_list_free(&entries);
}

static int rename_without_overwrite(const wchar_t *source, const wchar_t *target, Logger *logger, wchar_t **error_out) {
    *error_out = NULL;
    if (same_normalized_path(source, target)) {
        log_format(logger, L"건너뜀: 이미 대상 이름입니다 - %ls", source);
        return 0;
    }
    if (path_exists(target)) {
        size_t len = wcslen(target) + 64;
        wchar_t *err = (wchar_t *)xmalloc(len * sizeof(wchar_t));
        swprintf(err, len, L"대상 파일이 이미 있습니다: %ls", target);
        *error_out = err;
        return -1;
    }
    if (!MoveFileW(source, target)) {
        *error_out = last_error_message_dup(GetLastError());
        return -1;
    }
    log_format(logger, L"변경: %ls -> %ls", source, file_name_part(target));
    return 1;
}

static RenameStats change_extensions(const wchar_t *folder, int recursive, Logger *logger) {
    RenameStats stats;
    memset(&stats, 0, sizeof(stats));

    PathList files;
    path_list_init(&files);
    collect_files_recursive(folder, recursive, &files, logger);

    for (size_t i = 0; i < files.count; i++) {
        const wchar_t *file_path = files.items[i];
        stats.scanned++;

        wchar_t *target = NULL;
        const wchar_t *reason = L"";
        if (!extension_change_target(file_path, &target, &reason)) {
            stats.skipped++;
            log_format(logger, L"건너뜀: %ls (%ls)", file_path, reason);
            continue;
        }

        wchar_t *error = NULL;
        int result = rename_without_overwrite(file_path, target, logger, &error);
        if (result == 1) {
            stats.changed++;
        } else if (result == 0) {
            stats.skipped++;
        } else {
            stats.failed++;
            wchar_t *name = file_name_dup(file_path);
            stats_add_error(&stats.error_files, &stats.error_count, &stats.error_cap, name);
            free(name);
            log_format(logger, L"실패: %ls (%ls)", file_path, error ? error : L"알 수 없는 오류");
        }
        free(error);
        free(target);
    }

    path_list_free(&files);
    return stats;
}

static int suffix_equals_any(const wchar_t *path, const wchar_t **exts, size_t count) {
    wchar_t *suffix = suffix_dup(path);
    int found = 0;
    for (size_t i = 0; i < count; i++) {
        if (ci_equal(suffix, exts[i])) {
            found = 1;
            break;
        }
    }
    free(suffix);
    return found;
}

static int is_first_pass_archive(const wchar_t *path) {
    static const wchar_t *exts[] = { L".7z", L".zip", L".zi", L".001", L".rar" };
    return suffix_equals_any(path, exts, sizeof(exts) / sizeof(exts[0]));
}

static int is_second_pass_candidate(const wchar_t *path) {
    wchar_t *suffix = suffix_dup(path);
    int result = ci_equal(suffix, L".7z") || ci_equal(suffix, L".zip") ||
                 ci_equal(suffix, L".zi") || ci_equal(suffix, L".rar") ||
                 ci_starts_with(suffix, L".zip");
    free(suffix);
    return result;
}

static int part_rar_number(const wchar_t *path) {
    wchar_t *suffix = suffix_dup(path);
    if (!ci_equal(suffix, L".rar")) {
        free(suffix);
        return -1;
    }
    free(suffix);

    const wchar_t *name = file_name_part(path);
    const wchar_t *dot = last_suffix_dot(path);
    size_t stem_len = dot ? (size_t)(dot - name) : wcslen(name);
    wchar_t *stem = (wchar_t *)xmalloc((stem_len + 1) * sizeof(wchar_t));
    memcpy(stem, name, stem_len * sizeof(wchar_t));
    stem[stem_len] = L'\0';

    int number = -1;
    for (size_t i = 0; i < stem_len; i++) {
        int boundary = (i == 0) || stem[i - 1] == L'.' || stem[i - 1] == L'_' ||
                       stem[i - 1] == L' ' || stem[i - 1] == L'-';
        if (!boundary || !ci_equal_n(stem + i, L"part", 4)) {
            continue;
        }
        size_t pos = i + 4;
        if (pos >= stem_len || !iswdigit(stem[pos])) {
            continue;
        }
        int value = 0;
        while (pos < stem_len && iswdigit(stem[pos])) {
            value = value * 10 + (int)(stem[pos] - L'0');
            pos++;
        }
        if (pos == stem_len) {
            number = value;
        }
    }

    free(stem);
    return number;
}

static PathList find_first_pass_archives(const wchar_t *folder, int recursive, Logger *logger) {
    PathList files;
    PathList archives;
    path_list_init(&files);
    path_list_init(&archives);

    collect_files_recursive(folder, recursive, &files, logger);
    for (size_t i = 0; i < files.count; i++) {
        if (!is_first_pass_archive(files.items[i])) {
            continue;
        }
        int part = part_rar_number(files.items[i]);
        if (part >= 0 && part != 1) {
            log_format(logger, L"건너뜀: 분할 rar 후속 파일 - %ls", files.items[i]);
            continue;
        }
        path_list_append_dup(&archives, files.items[i]);
    }

    path_list_free(&files);
    qsort(archives.items, archives.count, sizeof(wchar_t *), compare_wstring_ptr_ci);
    return archives;
}

static int file_size_u64(const wchar_t *path, uint64_t *size_out) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data)) {
        return 0;
    }
    ULARGE_INTEGER value;
    value.HighPart = data.nFileSizeHigh;
    value.LowPart = data.nFileSizeLow;
    *size_out = value.QuadPart;
    return 1;
}

static wchar_t *pick_largest_second_pass_archive(const wchar_t *directory) {
    DirEntryList entries;
    if (!list_directory(directory, &entries, NULL)) {
        return NULL;
    }

    wchar_t *picked = NULL;
    uint64_t best_size = 0;
    int has_best = 0;
    for (size_t i = 0; i < entries.count; i++) {
        if (entries.items[i].attrs & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        if (!is_second_pass_candidate(entries.items[i].path)) {
            continue;
        }
        uint64_t size = 0;
        if (!file_size_u64(entries.items[i].path, &size)) {
            continue;
        }
        if (!has_best || size > best_size) {
            free(picked);
            picked = xwcsdup(entries.items[i].path);
            best_size = size;
            has_best = 1;
        }
    }

    dir_entry_list_free(&entries);
    return picked;
}

static wchar_t *prepare_second_pass_archive(const wchar_t *archive, Logger *logger, wchar_t **error_out) {
    *error_out = NULL;
    wchar_t *suffix = suffix_dup(archive);
    int should_rename = ci_equal(suffix, L".zi") || (ci_starts_with(suffix, L".zip") && !ci_equal(suffix, L".zip"));
    free(suffix);

    if (!should_rename) {
        return xwcsdup(archive);
    }

    wchar_t *target = path_with_suffix(archive, L".zip");
    if (same_normalized_path(archive, target)) {
        return target;
    }
    if (path_exists(target)) {
        size_t len = wcslen(target) + 80;
        wchar_t *err = (wchar_t *)xmalloc(len * sizeof(wchar_t));
        swprintf(err, len, L"변경할 대상 파일이 이미 있습니다: %ls", target);
        *error_out = err;
        free(target);
        return NULL;
    }
    if (!MoveFileW(archive, target)) {
        *error_out = last_error_message_dup(GetLastError());
        free(target);
        return NULL;
    }
    log_format(logger, L"2차 압축파일 이름 변경: %ls -> %ls", archive, file_name_part(target));
    return target;
}

static wchar_t *quote_arg(const wchar_t *arg) {
    size_t extra = 2;
    size_t len = wcslen(arg);
    for (size_t i = 0; i < len; i++) {
        if (arg[i] == L'"' || arg[i] == L'\\') {
            extra++;
        }
    }
    wchar_t *quoted = (wchar_t *)xmalloc((len + extra + 1) * sizeof(wchar_t));
    wchar_t *out = quoted;
    *out++ = L'"';
    size_t backslashes = 0;
    for (size_t i = 0; i < len; i++) {
        if (arg[i] == L'\\') {
            backslashes++;
            *out++ = L'\\';
        } else if (arg[i] == L'"') {
            for (size_t j = 0; j <= backslashes; j++) {
                *out++ = L'\\';
            }
            *out++ = L'"';
            backslashes = 0;
        } else {
            backslashes = 0;
            *out++ = arg[i];
        }
    }
    for (size_t j = 0; j < backslashes; j++) {
        *out++ = L'\\';
    }
    *out++ = L'"';
    *out = L'\0';
    return quoted;
}

static void append_wstr(wchar_t **buffer, size_t *len, size_t *cap, const wchar_t *text) {
    size_t add = wcslen(text);
    if (*len + add + 1 > *cap) {
        size_t next_cap = *cap ? *cap : 128;
        while (*len + add + 1 > next_cap) {
            next_cap *= 2;
        }
        *buffer = (wchar_t *)xrealloc(*buffer, next_cap * sizeof(wchar_t));
        *cap = next_cap;
    }
    memcpy(*buffer + *len, text, (add + 1) * sizeof(wchar_t));
    *len += add;
}

static wchar_t *build_bandizip_command_line(const wchar_t *bandizip, const wchar_t *archive, const wchar_t *output_dir, const wchar_t *password) {
    wchar_t *command = NULL;
    size_t len = 0;
    size_t cap = 0;

    wchar_t *quoted_exe = quote_arg(bandizip);
    wchar_t *quoted_output = NULL;
    wchar_t *quoted_password = NULL;
    wchar_t *quoted_archive = quote_arg(archive);

    size_t output_arg_len = wcslen(output_dir) + 4;
    wchar_t *output_arg = (wchar_t *)xmalloc((output_arg_len + 1) * sizeof(wchar_t));
    swprintf(output_arg, output_arg_len + 1, L"-o:%ls", output_dir);
    quoted_output = quote_arg(output_arg);

    append_wstr(&command, &len, &cap, quoted_exe);
    append_wstr(&command, &len, &cap, L" x -aoa ");
    append_wstr(&command, &len, &cap, quoted_output);

    if (password && password[0]) {
        size_t pass_arg_len = wcslen(password) + 4;
        wchar_t *pass_arg = (wchar_t *)xmalloc((pass_arg_len + 1) * sizeof(wchar_t));
        swprintf(pass_arg, pass_arg_len + 1, L"-p:%ls", password);
        quoted_password = quote_arg(pass_arg);
        append_wstr(&command, &len, &cap, L" ");
        append_wstr(&command, &len, &cap, quoted_password);
        free(pass_arg);
    }

    append_wstr(&command, &len, &cap, L" ");
    append_wstr(&command, &len, &cap, quoted_archive);

    free(quoted_exe);
    free(output_arg);
    free(quoted_output);
    free(quoted_password);
    free(quoted_archive);
    return command;
}

static int run_bandizip(const wchar_t *bandizip, const wchar_t *archive, const wchar_t *output_dir, const wchar_t *password, wchar_t **error_out) {
    *error_out = NULL;
    if (!ensure_directory(output_dir)) {
        *error_out = last_error_message_dup(GetLastError());
        return 0;
    }

    wchar_t *command_line = build_bandizip_command_line(bandizip, archive, output_dir, password);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);

    DWORD flags = 0;
    if (ci_equal(file_name_part(bandizip), L"bz.exe")) {
        flags |= CREATE_NO_WINDOW;
    }

    BOOL ok = CreateProcessW(bandizip, command_line, NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi);
    if (!ok) {
        *error_out = last_error_message_dup(GetLastError());
        free(command_line);
        return 0;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    free(command_line);

    if (exit_code != 0) {
        wchar_t buffer[128];
        swprintf(buffer, 128, L"Bandizip 종료 코드 %lu", exit_code);
        *error_out = xwcsdup(buffer);
        return 0;
    }
    return 1;
}

static int path_list_contains_norm(PathList *list, const wchar_t *path) {
    for (size_t i = 0; i < list->count; i++) {
        if (same_normalized_path(list->items[i], path)) {
            return 1;
        }
    }
    return 0;
}

static ExtractStats extract_archives(const wchar_t *folder, int recursive, const wchar_t *bandizip, const wchar_t *password, Logger *logger) {
    ExtractStats stats;
    memset(&stats, 0, sizeof(stats));

    PathList archives = find_first_pass_archives(folder, recursive, logger);
    stats.first_targets = archives.count;
    log_format(logger, L"1차 압축 대상: %zu개", stats.first_targets);

    PathList extracted_roots;
    path_list_init(&extracted_roots);
    for (size_t i = 0; i < archives.count; i++) {
        wchar_t *output_dir = path_remove_suffix(archives.items[i]);
        wchar_t *error = NULL;
        log_format(logger, L"1차 압축풀기: %ls", archives.items[i]);
        if (run_bandizip(bandizip, archives.items[i], output_dir, password, &error)) {
            path_list_append_owned(&extracted_roots, output_dir);
            output_dir = NULL;
            stats.first_success++;
        } else {
            stats.first_failed++;
            wchar_t *name = file_name_dup(archives.items[i]);
            stats_add_error(&stats.error_files, &stats.error_count, &stats.error_cap, name);
            free(name);
            log_format(logger, L"1차 실패: %ls (%ls)", archives.items[i], error ? error : L"알 수 없는 오류");
        }
        free(output_dir);
        free(error);
    }

    log_text(logger, L"1차 압축풀기 작업이 끝났습니다. 2차 압축파일을 검색합니다.");

    PathList visited_dirs;
    PathList second_archives;
    path_list_init(&visited_dirs);
    path_list_init(&second_archives);

    for (size_t i = 0; i < extracted_roots.count; i++) {
        if (!path_is_dir(extracted_roots.items[i])) {
            log_format(logger, L"건너뜀: 압축풀기 폴더가 없습니다 - %ls", extracted_roots.items[i]);
            continue;
        }
        PathList dirs;
        path_list_init(&dirs);
        collect_directories_recursive(extracted_roots.items[i], &dirs, logger);
        for (size_t d = 0; d < dirs.count; d++) {
            if (path_list_contains_norm(&visited_dirs, dirs.items[d])) {
                continue;
            }
            path_list_append_dup(&visited_dirs, dirs.items[d]);
            wchar_t *picked = pick_largest_second_pass_archive(dirs.items[d]);
            if (picked) {
                path_list_append_owned(&second_archives, picked);
            }
        }
        path_list_free(&dirs);
    }

    stats.second_targets = second_archives.count;
    log_format(logger, L"2차 압축 대상: %zu개", stats.second_targets);

    PathList processed;
    path_list_init(&processed);
    for (size_t i = 0; i < second_archives.count; i++) {
        wchar_t *error = NULL;
        wchar_t *prepared = prepare_second_pass_archive(second_archives.items[i], logger, &error);
        if (!prepared) {
            stats.second_failed++;
            wchar_t *name = file_name_dup(second_archives.items[i]);
            stats_add_error(&stats.error_files, &stats.error_count, &stats.error_cap, name);
            free(name);
            log_format(logger, L"2차 실패: %ls (%ls)", second_archives.items[i], error ? error : L"알 수 없는 오류");
            free(error);
            continue;
        }
        free(error);

        if (path_list_contains_norm(&processed, prepared)) {
            free(prepared);
            continue;
        }
        path_list_append_dup(&processed, prepared);

        wchar_t *output_dir = parent_path_dup(prepared);
        log_format(logger, L"2차 압축풀기: %ls", prepared);
        if (run_bandizip(bandizip, prepared, output_dir, password, &error)) {
            stats.second_success++;
        } else {
            stats.second_failed++;
            wchar_t *name = file_name_dup(prepared);
            stats_add_error(&stats.error_files, &stats.error_count, &stats.error_cap, name);
            free(name);
            log_format(logger, L"2차 실패: %ls (%ls)", prepared, error ? error : L"알 수 없는 오류");
        }
        free(error);
        free(output_dir);
        free(prepared);
    }

    path_list_free(&archives);
    path_list_free(&extracted_roots);
    path_list_free(&visited_dirs);
    path_list_free(&second_archives);
    path_list_free(&processed);
    return stats;
}

static void log_error_file_names(wchar_t **error_files, size_t count, Logger *logger) {
    if (count == 0) {
        return;
    }
    log_text(logger, L"오류 발생 파일명:");
    for (size_t i = 0; i < count; i++) {
        log_text(logger, error_files[i]);
    }
}

#ifndef BAIDU_RENAMER_TEST

static void gui_log_callback(Logger *logger, const wchar_t *message) {
    AppState *app = (AppState *)logger->ctx;
    PostMessageW(app->hwnd, WM_APP_LOG, 0, (LPARAM)xwcsdup(message));
}

static void set_child_font(HWND hwnd, HFONT font) {
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
}

static HWND create_control(AppState *app, const wchar_t *class_name, const wchar_t *text, DWORD style, DWORD ex_style, int id) {
    HWND hwnd = CreateWindowExW(
        ex_style,
        class_name,
        text,
        WS_CHILD | style,
        0,
        0,
        10,
        10,
        app->hwnd,
        (HMENU)(INT_PTR)id,
        GetModuleHandleW(NULL),
        NULL);
    set_child_font(hwnd, app->font);
    return hwnd;
}

static void append_log_to_edit(AppState *app, const wchar_t *message) {
    int len = GetWindowTextLengthW(app->log_edit);
    SendMessageW(app->log_edit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(app->log_edit, EM_REPLACESEL, FALSE, (LPARAM)message);
    SendMessageW(app->log_edit, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessageW(app->log_edit, EM_SCROLLCARET, 0, 0);
}

static wchar_t *get_window_text_dup(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    wchar_t *buffer = (wchar_t *)xmalloc(((size_t)len + 1) * sizeof(wchar_t));
    GetWindowTextW(hwnd, buffer, len + 1);
    return buffer;
}

static int checkbox_checked(HWND hwnd) {
    return SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void show_tab_controls(AppState *app) {
    int ext = app->active_tab == 0;
    ShowWindow(app->ext_label_folder, ext ? SW_SHOW : SW_HIDE);
    ShowWindow(app->ext_folder, ext ? SW_SHOW : SW_HIDE);
    ShowWindow(app->ext_browse, ext ? SW_SHOW : SW_HIDE);
    ShowWindow(app->ext_recursive, ext ? SW_SHOW : SW_HIDE);
    ShowWindow(app->ext_start, ext ? SW_SHOW : SW_HIDE);

    ShowWindow(app->extract_label_folder, ext ? SW_HIDE : SW_SHOW);
    ShowWindow(app->extract_folder, ext ? SW_HIDE : SW_SHOW);
    ShowWindow(app->extract_browse, ext ? SW_HIDE : SW_SHOW);
    ShowWindow(app->extract_recursive, ext ? SW_HIDE : SW_SHOW);
    ShowWindow(app->label_bandizip, ext ? SW_HIDE : SW_SHOW);
    ShowWindow(app->bandizip, ext ? SW_HIDE : SW_SHOW);
    ShowWindow(app->bandizip_browse, ext ? SW_HIDE : SW_SHOW);
    ShowWindow(app->label_password, ext ? SW_HIDE : SW_SHOW);
    ShowWindow(app->password, ext ? SW_HIDE : SW_SHOW);
    ShowWindow(app->extract_start, ext ? SW_HIDE : SW_SHOW);
}

static void layout_controls(AppState *app, int width, int height) {
    const int margin = 12;
    const int label_w = 120;
    const int button_w = 76;
    const int row_h = 28;
    const int gap = 8;
    const int tab_top = 50;
    const int tab_h = 205;
    const int log_top = tab_top + tab_h + 10;
    int content_w = width - margin * 2;
    int edit_w = content_w - label_w - button_w - gap * 2;
    if (edit_w < 160) {
        edit_w = 160;
    }

    MoveWindow(app->title, margin, 10, 300, 30, TRUE);
    MoveWindow(app->status, width - margin - 130, 16, 130, 24, TRUE);
    MoveWindow(app->tab, margin, tab_top, content_w, tab_h, TRUE);

    int x0 = margin + 18;
    int y0 = tab_top + 44;
    int edit_x = x0 + label_w + gap;
    int button_x = edit_x + edit_w + gap;

    MoveWindow(app->ext_label_folder, x0, y0, label_w, row_h, TRUE);
    MoveWindow(app->ext_folder, edit_x, y0, edit_w, row_h, TRUE);
    MoveWindow(app->ext_browse, button_x, y0, button_w, row_h, TRUE);
    MoveWindow(app->ext_recursive, edit_x, y0 + 40, 150, row_h, TRUE);
    MoveWindow(app->ext_start, edit_x, y0 + 82, 150, 32, TRUE);

    MoveWindow(app->extract_label_folder, x0, y0, label_w, row_h, TRUE);
    MoveWindow(app->extract_folder, edit_x, y0, edit_w, row_h, TRUE);
    MoveWindow(app->extract_browse, button_x, y0, button_w, row_h, TRUE);
    MoveWindow(app->extract_recursive, edit_x, y0 + 40, 150, row_h, TRUE);
    MoveWindow(app->label_bandizip, x0, y0 + 80, label_w, row_h, TRUE);
    MoveWindow(app->bandizip, edit_x, y0 + 80, edit_w, row_h, TRUE);
    MoveWindow(app->bandizip_browse, button_x, y0 + 80, button_w, row_h, TRUE);
    MoveWindow(app->label_password, x0, y0 + 120, label_w, row_h, TRUE);
    MoveWindow(app->password, edit_x, y0 + 120, edit_w, row_h, TRUE);
    MoveWindow(app->extract_start, edit_x, y0 + 162, 150, 32, TRUE);

    int log_h = height - log_top - margin;
    if (log_h < 140) {
        log_h = 140;
    }
    MoveWindow(app->log_group, margin, log_top, content_w, log_h, TRUE);
    MoveWindow(app->log_edit, margin + 10, log_top + 24, content_w - 20, log_h - 68, TRUE);
    MoveWindow(app->clear_log, width - margin - 104, log_top + log_h - 36, 92, 26, TRUE);
}

static wchar_t *pick_folder_dialog(HWND owner, const wchar_t *initial) {
    BROWSEINFOW bi;
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = owner;
    bi.lpszTitle = L"폴더를 선택하세요";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    (void)initial;

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) {
        return NULL;
    }
    wchar_t path[MAX_PATH];
    int ok = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return ok ? xwcsdup(path) : NULL;
}

static wchar_t *pick_bandizip_dialog(HWND owner, const wchar_t *current) {
    wchar_t file[MAX_PATH * 2];
    wcsncpy(file, current ? current : L"", sizeof(file) / sizeof(file[0]) - 1);
    file[(sizeof(file) / sizeof(file[0])) - 1] = L'\0';

    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"실행 파일 (*.exe)\0*.exe\0모든 파일 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file) / sizeof(file[0]);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    return GetOpenFileNameW(&ofn) ? xwcsdup(file) : NULL;
}

static void free_worker_args(WorkerArgs *args) {
    if (!args) {
        return;
    }
    free(args->folder);
    free(args->bandizip);
    free(args->password);
    free(args);
}

static DWORD WINAPI worker_thread(LPVOID param) {
    WorkerArgs *args = (WorkerArgs *)param;
    int ok = 1;
    Logger logger;
    logger.callback = gui_log_callback;
    logger.ctx = args->app;

    if (args->task == TASK_EXTENSION) {
        log_format(&logger, L"확장자 변경 시작: %ls", args->folder);
        RenameStats stats = change_extensions(args->folder, args->recursive, &logger);
        log_format(
            &logger,
            L"확장자 변경 완료: 검색 %zu개, 변경 %zu개, 건너뜀 %zu개, 실패 %zu개",
            stats.scanned,
            stats.changed,
            stats.skipped,
            stats.failed);
        log_error_file_names(stats.error_files, stats.error_count, &logger);
        ok = stats.failed == 0;
        rename_stats_free(&stats);
    } else {
        log_format(&logger, L"다중 압축 풀기 시작: %ls", args->folder);
        ExtractStats stats = extract_archives(args->folder, args->recursive, args->bandizip, args->password, &logger);
        log_format(
            &logger,
            L"다중 압축 풀기 완료: 1차 대상 %zu개, 1차 성공 %zu개, 1차 실패 %zu개, 2차 대상 %zu개, 2차 성공 %zu개, 2차 실패 %zu개",
            stats.first_targets,
            stats.first_success,
            stats.first_failed,
            stats.second_targets,
            stats.second_success,
            stats.second_failed);
        log_error_file_names(stats.error_files, stats.error_count, &logger);
        ok = stats.first_failed == 0 && stats.second_failed == 0;
        extract_stats_free(&stats);
    }

    PostMessageW(args->app->hwnd, WM_APP_DONE, (WPARAM)args->task, (LPARAM)ok);
    free_worker_args(args);
    return 0;
}

static int validate_path(HWND owner, const wchar_t *value, const wchar_t *label, int want_file, wchar_t **out) {
    wchar_t *path = trim_path_input(value);
    int ok = want_file ? path_is_file(path) : path_is_dir(path);
    if (!ok) {
        wchar_t message[4096];
        swprintf(message, sizeof(message) / sizeof(message[0]), L"%ls%ls 확인할 수 없습니다.\n%ls", label, want_file ? L"을" : L"를", path);
        MessageBoxW(owner, message, APP_NAME, MB_ICONERROR | MB_OK);
        free(path);
        return 0;
    }
    *out = path;
    return 1;
}

static void start_extension_task(AppState *app) {
    wchar_t *folder_text = get_window_text_dup(app->ext_folder);
    wchar_t *folder = NULL;
    if (!validate_path(app->hwnd, folder_text, L"대상 폴더", 0, &folder)) {
        free(folder_text);
        return;
    }
    free(folder_text);

    WorkerArgs *args = (WorkerArgs *)xmalloc(sizeof(WorkerArgs));
    memset(args, 0, sizeof(*args));
    args->app = app;
    args->task = TASK_EXTENSION;
    args->folder = folder;
    args->recursive = checkbox_checked(app->ext_recursive);

    EnableWindow(app->ext_start, FALSE);
    SetWindowTextW(app->status, L"작업 중");
    HANDLE thread = CreateThread(NULL, 0, worker_thread, args, 0, NULL);
    if (!thread) {
        MessageBoxW(app->hwnd, L"작업 스레드를 시작할 수 없습니다.", APP_NAME, MB_ICONERROR | MB_OK);
        EnableWindow(app->ext_start, TRUE);
        SetWindowTextW(app->status, L"오류");
        free_worker_args(args);
        return;
    }
    CloseHandle(thread);
}

static void start_extract_task(AppState *app) {
    wchar_t *folder_text = get_window_text_dup(app->extract_folder);
    wchar_t *bandizip_text = get_window_text_dup(app->bandizip);
    wchar_t *password_text = get_window_text_dup(app->password);
    wchar_t *folder = NULL;
    wchar_t *bandizip = NULL;

    if (!validate_path(app->hwnd, folder_text, L"압축파일 폴더", 0, &folder)) {
        free(folder_text);
        free(bandizip_text);
        free(password_text);
        return;
    }
    if (!validate_path(app->hwnd, bandizip_text, L"반디집 실행 파일", 1, &bandizip)) {
        free(folder);
        free(folder_text);
        free(bandizip_text);
        free(password_text);
        return;
    }

    WorkerArgs *args = (WorkerArgs *)xmalloc(sizeof(WorkerArgs));
    memset(args, 0, sizeof(*args));
    args->app = app;
    args->task = TASK_EXTRACT;
    args->folder = folder;
    args->recursive = checkbox_checked(app->extract_recursive);
    args->bandizip = bandizip;
    args->password = password_text;

    free(folder_text);
    free(bandizip_text);

    EnableWindow(app->extract_start, FALSE);
    SetWindowTextW(app->status, L"작업 중");
    HANDLE thread = CreateThread(NULL, 0, worker_thread, args, 0, NULL);
    if (!thread) {
        MessageBoxW(app->hwnd, L"작업 스레드를 시작할 수 없습니다.", APP_NAME, MB_ICONERROR | MB_OK);
        EnableWindow(app->extract_start, TRUE);
        SetWindowTextW(app->status, L"오류");
        free_worker_args(args);
        return;
    }
    CloseHandle(thread);
}

static void create_app_controls(AppState *app) {
    NONCLIENTMETRICSW ncm;
    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    app->font = CreateFontIndirectW(&ncm.lfMessageFont);

    LOGFONTW title_lf = ncm.lfMessageFont;
    title_lf.lfHeight = -22;
    title_lf.lfWeight = FW_BOLD;
    wcscpy(title_lf.lfFaceName, L"맑은 고딕");
    app->title_font = CreateFontIndirectW(&title_lf);

    app->title = create_control(app, L"STATIC", APP_NAME, WS_VISIBLE, 0, -1);
    set_child_font(app->title, app->title_font);
    app->status = create_control(app, L"STATIC", L"대기 중", WS_VISIBLE | SS_RIGHT, 0, IDC_STATUS);

    app->tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 10, 10, app->hwnd, (HMENU)IDC_TAB, GetModuleHandleW(NULL), NULL);
    set_child_font(app->tab, app->font);
    TCITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = TCIF_TEXT;
    item.pszText = L"파일 확장자 변경";
    TabCtrl_InsertItem(app->tab, 0, &item);
    item.pszText = L"다중 압축 풀기";
    TabCtrl_InsertItem(app->tab, 1, &item);

    app->ext_label_folder = create_control(app, L"STATIC", L"대상 폴더", WS_VISIBLE, 0, -1);
    app->ext_folder = create_control(app, L"EDIT", DEFAULT_DOWNLOAD_DIR, WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_EXT_FOLDER);
    app->ext_browse = create_control(app, L"BUTTON", L"찾기", WS_VISIBLE | BS_PUSHBUTTON, 0, IDC_EXT_BROWSE);
    app->ext_recursive = create_control(app, L"BUTTON", L"하위폴더 포함", WS_VISIBLE | BS_AUTOCHECKBOX, 0, IDC_EXT_RECURSIVE);
    SendMessageW(app->ext_recursive, BM_SETCHECK, BST_CHECKED, 0);
    app->ext_start = create_control(app, L"BUTTON", L"확장자 변경 시작", WS_VISIBLE | BS_PUSHBUTTON, 0, IDC_EXT_START);

    app->extract_label_folder = create_control(app, L"STATIC", L"압축파일 폴더", 0, 0, -1);
    app->extract_folder = create_control(app, L"EDIT", DEFAULT_DOWNLOAD_DIR, WS_BORDER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_EXTRACT_FOLDER);
    app->extract_browse = create_control(app, L"BUTTON", L"찾기", BS_PUSHBUTTON, 0, IDC_EXTRACT_BROWSE);
    app->extract_recursive = create_control(app, L"BUTTON", L"하위폴더 포함", BS_AUTOCHECKBOX, 0, IDC_EXTRACT_RECURSIVE);
    SendMessageW(app->extract_recursive, BM_SETCHECK, BST_CHECKED, 0);
    app->label_bandizip = create_control(app, L"STATIC", L"반디집 실행 파일", 0, 0, -1);
    app->bandizip = create_control(app, L"EDIT", DEFAULT_BANDIZIP, WS_BORDER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_BANDIZIP);
    app->bandizip_browse = create_control(app, L"BUTTON", L"찾기", BS_PUSHBUTTON, 0, IDC_BANDIZIP_BROWSE);
    app->label_password = create_control(app, L"STATIC", L"비밀번호", 0, 0, -1);
    app->password = create_control(app, L"EDIT", DEFAULT_PASSWORD, WS_BORDER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_PASSWORD);
    app->extract_start = create_control(app, L"BUTTON", L"압축 풀기 시작", BS_PUSHBUTTON, 0, IDC_EXTRACT_START);

    app->log_group = create_control(app, L"BUTTON", L"작업 로그", WS_VISIBLE | BS_GROUPBOX, 0, -1);
    app->log_edit = create_control(app, L"EDIT", L"", WS_VISIBLE | WS_VSCROLL | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, WS_EX_CLIENTEDGE, IDC_LOG);
    app->clear_log = create_control(app, L"BUTTON", L"로그 지우기", WS_VISIBLE | BS_PUSHBUTTON, 0, IDC_CLEAR_LOG);

    show_tab_controls(app);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    AppState *app = (AppState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
        app = (AppState *)cs->lpCreateParams;
        app->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)app);
        create_app_controls(app);
        return 0;
    }
    case WM_SIZE:
        if (app) {
            layout_controls(app, LOWORD(lparam), HIWORD(lparam));
        }
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lparam;
        mmi->ptMinTrackSize.x = 760;
        mmi->ptMinTrackSize.y = 520;
        return 0;
    }
    case WM_NOTIFY:
        if (app && ((LPNMHDR)lparam)->idFrom == IDC_TAB && ((LPNMHDR)lparam)->code == TCN_SELCHANGE) {
            app->active_tab = TabCtrl_GetCurSel(app->tab);
            show_tab_controls(app);
        }
        return 0;
    case WM_COMMAND:
        if (!app) {
            break;
        }
        switch (LOWORD(wparam)) {
        case IDC_EXT_BROWSE: {
            wchar_t *current = get_window_text_dup(app->ext_folder);
            wchar_t *picked = pick_folder_dialog(hwnd, current);
            if (picked) {
                SetWindowTextW(app->ext_folder, picked);
                free(picked);
            }
            free(current);
            return 0;
        }
        case IDC_EXTRACT_BROWSE: {
            wchar_t *current = get_window_text_dup(app->extract_folder);
            wchar_t *picked = pick_folder_dialog(hwnd, current);
            if (picked) {
                SetWindowTextW(app->extract_folder, picked);
                free(picked);
            }
            free(current);
            return 0;
        }
        case IDC_BANDIZIP_BROWSE: {
            wchar_t *current = get_window_text_dup(app->bandizip);
            wchar_t *picked = pick_bandizip_dialog(hwnd, current);
            if (picked) {
                SetWindowTextW(app->bandizip, picked);
                free(picked);
            }
            free(current);
            return 0;
        }
        case IDC_CLEAR_LOG:
            SetWindowTextW(app->log_edit, L"");
            return 0;
        case IDC_EXT_START:
            start_extension_task(app);
            return 0;
        case IDC_EXTRACT_START:
            start_extract_task(app);
            return 0;
        }
        break;
    case WM_APP_LOG:
        if (app) {
            wchar_t *message = (wchar_t *)lparam;
            append_log_to_edit(app, message);
            free(message);
        }
        return 0;
    case WM_APP_DONE:
        if (app) {
            TaskKind task = (TaskKind)wparam;
            int ok = (int)lparam;
            if (task == TASK_EXTENSION) {
                EnableWindow(app->ext_start, TRUE);
            } else if (task == TASK_EXTRACT) {
                EnableWindow(app->extract_start, TRUE);
            }
            SetWindowTextW(app->status, ok ? L"완료" : L"오류");
        }
        return 0;
    case WM_DESTROY:
        if (app) {
            DeleteObject(app->font);
            DeleteObject(app->title_font);
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev_instance, PWSTR cmd_line, int show_cmd) {
    (void)prev_instance;
    (void)cmd_line;

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.lpszClassName = L"BaiduRenamerWindow";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    if (!RegisterClassW(&wc)) {
        MessageBoxW(NULL, L"창 클래스를 등록할 수 없습니다.", APP_NAME, MB_ICONERROR | MB_OK);
        CoUninitialize();
        return 1;
    }

    AppState app;
    memset(&app, 0, sizeof(app));
    app.active_tab = 0;

    HWND hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        APP_NAME,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        860,
        620,
        NULL,
        NULL,
        instance,
        &app);

    if (!hwnd) {
        MessageBoxW(NULL, L"창을 만들 수 없습니다.", APP_NAME, MB_ICONERROR | MB_OK);
        CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, show_cmd);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}

#else

static void console_log_callback(Logger *logger, const wchar_t *message) {
    (void)logger;
    fwprintf(stdout, L"%ls\n", message);
}

static int expect_target(const wchar_t *path, const wchar_t *expected) {
    wchar_t *target = NULL;
    const wchar_t *reason = NULL;
    int changed = extension_change_target(path, &target, &reason);
    int ok = changed && target && ci_equal(target, expected);
    if (!ok) {
        fwprintf(stderr, L"FAIL target: %ls -> %ls (%ls), expected %ls\n", path, target ? target : L"<skip>", reason ? reason : L"", expected);
    }
    free(target);
    return ok;
}

static int expect_skip(const wchar_t *path) {
    wchar_t *target = NULL;
    const wchar_t *reason = NULL;
    int changed = extension_change_target(path, &target, &reason);
    int ok = !changed && target == NULL;
    if (!ok) {
        fwprintf(stderr, L"FAIL skip: %ls -> %ls (%ls)\n", path, target ? target : L"<skip>", reason ? reason : L"");
    }
    free(target);
    return ok;
}

int wmain(void) {
    SetConsoleOutputCP(CP_UTF8);

    int ok = 1;
    ok &= expect_target(L"C:\\T\\26.06.mp4", L"C:\\T\\26.06.zip");
    ok &= expect_target(L"C:\\T\\2026.06.MP4", L"C:\\T\\2026.06.zip");
    ok &= expect_skip(L"C:\\T\\신난다2026.06.mp4");
    ok &= expect_target(L"C:\\T\\26.13.mp4", L"C:\\T\\26.13.zip");
    ok &= expect_target(L"C:\\T\\abc.7foo.mp4", L"C:\\T\\abc.7foo");
    ok &= expect_skip(L"C:\\T\\abc.06.mp4");

    Logger logger;
    logger.callback = console_log_callback;
    logger.ctx = NULL;

    wchar_t temp[MAX_PATH];
    DWORD temp_len = GetTempPathW(MAX_PATH, temp);
    if (temp_len == 0 || temp_len >= MAX_PATH) {
        fwprintf(stderr, L"FAIL temp path\n");
        return 1;
    }
    wchar_t dir[MAX_PATH];
    swprintf(dir, MAX_PATH, L"%lsBaiduRenamerTest_%lu", temp, GetTickCount());
    CreateDirectoryW(dir, NULL);
    wchar_t *odd_zip = path_join(dir, L"신난다.zip만세");
    HANDLE file = CreateFileW(odd_zip, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"FAIL create odd zip\n");
        ok = 0;
    } else {
        DWORD written = 0;
        WriteFile(file, "x", 1, &written, NULL);
        CloseHandle(file);
        if (!is_second_pass_candidate(odd_zip)) {
            fwprintf(stderr, L"FAIL second candidate: %ls\n", odd_zip);
            ok = 0;
        }
        wchar_t *error = NULL;
        wchar_t *prepared = prepare_second_pass_archive(odd_zip, &logger, &error);
        wchar_t *expected = path_join(dir, L"신난다.zip");
        if (!prepared || !ci_equal(prepared, expected) || !path_is_file(expected)) {
            fwprintf(stderr, L"FAIL prepare second: got %ls expected %ls error %ls\n", prepared ? prepared : L"<null>", expected, error ? error : L"");
            ok = 0;
        }
        DeleteFileW(expected);
        free(prepared);
        free(expected);
        free(error);
    }
    RemoveDirectoryW(dir);
    free(odd_zip);

    if (ok) {
        fwprintf(stdout, L"self-test passed\n");
    }
    return ok ? 0 : 1;
}

#endif

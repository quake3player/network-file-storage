#define _POSIX_C_SOURCE 200809L

#include "net.h"
#include "persistence.h"
#include "protocol.h"
#include "file_index.h"

#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

typedef enum {
    ROLE_PENDING = 0,
    ROLE_STORAGE_SERVER,
    ROLE_CLIENT
} connection_role_t;

typedef struct connection {
    int fd;
    connection_role_t role;
    char identifier[128];
    char session_token[64];
    struct connection *next;
} connection_t;

typedef struct storage_server_info {
    char ss_id[128];
    char ip[64];
    uint16_t client_port;
    uint32_t active_readers;
    uint32_t active_writers;
    time_t last_heartbeat;
    struct storage_server_info *next;
} storage_server_info_t;

typedef struct client_info {
    char username[128];
    char ip[64];
    uint16_t client_port;
    time_t registered_at;
    struct client_info *next;
} client_info_t;

typedef struct {
    int listen_fd;
    connection_t *connections;
    storage_server_info_t *servers;
    client_info_t *clients;
    unsigned long session_seq;
    char data_dir[PATH_MAX];
    FILE *log_file;
    file_index_t *file_index;
    lru_cache_t *cache;
    unsigned int next_primary_index;  /* Round-robin counter for primary selection */
} nm_state_t;

typedef struct {
    char username[128];
    int can_write;
    time_t requested_at;
} access_request_t;

static volatile sig_atomic_t keep_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    keep_running = 0;
}

static int ensure_parent_dirs(const char *path) {
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }
    char temp[PATH_MAX];
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    char *slash = strrchr(temp, '/');
    if (slash == NULL) {
        return 0;
    }
    *slash = '\0';
    if (temp[0] == '\0') {
        return 0;
    }
    for (char *p = temp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(temp, 0755);
            *p = '/';
        }
    }
    mkdir(temp, 0755);
    return 0;
}

static int persist_acl_file(nm_state_t *state, const file_record_t *record) {
    if (state == NULL || record == NULL) {
        return -1;
    }
    char acl_path[PATH_MAX];
    if (ns_acl_path(acl_path, sizeof(acl_path), state->data_dir, record->filename) != 0) {
        return -1;
    }
    if (ensure_parent_dirs(acl_path) != 0) {
        return -1;
    }
    FILE *acl_file = fopen(acl_path, "w");
    if (acl_file == NULL) {
        return -1;
    }
    fprintf(acl_file, "%s:RW\n", record->owner);
    for (size_t i = 0; i < record->acl_count; i++) {
        const acl_entry_t *entry = &record->acl[i];
        if (!entry->can_read && !entry->can_write) {
            continue;
        }
        if (entry->can_read && entry->can_write) {
            fprintf(acl_file, "%s:RW\n", entry->username);
        } else if (entry->can_write) {
            fprintf(acl_file, "%s:W\n", entry->username);
        } else if (entry->can_read) {
            fprintf(acl_file, "%s:R\n", entry->username);
        }
    }
    fclose(acl_file);
    return 0;
}

static int load_access_requests(nm_state_t *state, const char *filename, access_request_t **out_requests, size_t *out_count) {
    if (state == NULL || filename == NULL || out_requests == NULL || out_count == NULL) {
        return -1;
    }
    *out_requests = NULL;
    *out_count = 0;

    char path[PATH_MAX];
    if (ns_requests_path(path, sizeof(path), state->data_dir, filename) != 0) {
        return -1;
    }
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }

    size_t capacity = 8;
    access_request_t *requests = calloc(capacity, sizeof(*requests));
    if (requests == NULL) {
        fclose(fp);
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char user[128] = {0};
        char mode[16] = {0};
        long long ts = 0;
        if (sscanf(line, "%127[^|]|%15[^|]|%lld", user, mode, &ts) != 3) {
            continue;
        }
        if (*out_count >= capacity) {
            capacity *= 2;
            access_request_t *tmp = realloc(requests, capacity * sizeof(*requests));
            if (tmp == NULL) {
                free(requests);
                fclose(fp);
                return -1;
            }
            requests = tmp;
        }
        access_request_t *req = &requests[*out_count];
        strncpy(req->username, user, sizeof(req->username) - 1);
        req->can_write = (strcmp(mode, "write") == 0);
        req->requested_at = (time_t)ts;
        (*out_count)++;
    }
    fclose(fp);
    *out_requests = requests;
    return 0;
}

static int save_access_requests(nm_state_t *state, const char *filename, const access_request_t *requests, size_t count) {
    if (state == NULL || filename == NULL) {
        return -1;
    }
    char path[PATH_MAX];
    if (ns_requests_path(path, sizeof(path), state->data_dir, filename) != 0) {
        return -1;
    }
    if (ensure_parent_dirs(path) != 0) {
        return -1;
    }
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        return -1;
    }
    for (size_t i = 0; i < count; i++) {
        fprintf(fp, "%s|%s|%lld\n", requests[i].username, requests[i].can_write ? "write" : "read", (long long)requests[i].requested_at);
    }
    fclose(fp);
    return 0;
}

static int find_request_index(const access_request_t *requests, size_t count, const char *username, int can_write) {
    if (requests == NULL || username == NULL) {
        return -1;
    }
    for (size_t i = 0; i < count; i++) {
        if (strcmp(requests[i].username, username) == 0 && requests[i].can_write == can_write) {
            return (int)i;
        }
    }
    return -1;
}

static void remove_request_at(access_request_t *requests, size_t *count, size_t idx) {
    if (requests == NULL || count == NULL || idx >= *count) {
        return;
    }
    memmove(&requests[idx], &requests[idx + 1], (*count - idx - 1) * sizeof(*requests));
    (*count)--;
}

static int find_request_by_user(const access_request_t *requests, size_t count, const char *username) {
    if (requests == NULL || username == NULL) {
        return -1;
    }
    for (size_t i = 0; i < count; i++) {
        if (strcmp(requests[i].username, username) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int parse_access_mode(const char *mode_value, int *can_write_out) {
    if (can_write_out == NULL) {
        return -1;
    }
    *can_write_out = 0;
    if (mode_value == NULL || mode_value[0] == '\0') {
        return 0;
    }
    char lower[16];
    size_t len = strlen(mode_value);
    if (len >= sizeof(lower)) {
        len = sizeof(lower) - 1;
    }
    for (size_t i = 0; i < len; i++) {
        lower[i] = (char)tolower((unsigned char)mode_value[i]);
    }
    lower[len] = '\0';
    if (strcmp(lower, "read") == 0) {
        *can_write_out = 0;
        return 0;
    }
    if (strcmp(lower, "write") == 0) {
        *can_write_out = 1;
        return 0;
    }
    return -1;
}

static void format_time_string(time_t when, char *buf, size_t len) {
    if (buf == NULL || len == 0) {
        return;
    }
    if (when == 0) {
        snprintf(buf, len, "unknown");
        return;
    }
    struct tm tm_snapshot;
    if (localtime_r(&when, &tm_snapshot) == NULL) {
        snprintf(buf, len, "unknown");
        return;
    }
    strftime(buf, len, "%Y-%m-%d %H:%M", &tm_snapshot);
}

static int ensure_dir(const char *path) {
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (mkdir(path, 0755) == -1 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static void nm_log(nm_state_t *state, const char *fmt, ...) {
    if (state == NULL || fmt == NULL) {
        return;
    }

    time_t now = time(NULL);
    struct tm tm_snapshot;
    localtime_r(&now, &tm_snapshot);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_snapshot);

    /* ANSI colors for logs */
    const char *COLOR_CYAN = "\033[36m";
    const char *COLOR_RESET = "\033[0m";
    const char *COLOR_YELLOW = "\033[33m";
    const char *COLOR_RED = "\033[31m";
    const char *COLOR_GREEN = "\033[32m";

    const char *color = COLOR_RESET;
    if (strstr(fmt, "Error") || strstr(fmt, "Failed") || strstr(fmt, "dead") || strstr(fmt, "denied")) {
        color = COLOR_RED;
    } else if (strstr(fmt, "Warning") || strstr(fmt, "Skipping")) {
        color = COLOR_YELLOW;
    } else if (strstr(fmt, "Registered") || strstr(fmt, "Created") || strstr(fmt, "Approved")) {
        color = COLOR_GREEN;
    }

    va_list args;
    va_start(args, fmt);
    fprintf(stdout, "%s[%s]%s ", COLOR_CYAN, timestamp, COLOR_RESET);
    fprintf(stdout, "%s", color);
    vfprintf(stdout, fmt, args);
    fprintf(stdout, "%s\n", COLOR_RESET);
    fflush(stdout);
    va_end(args);

    if (state->log_file != NULL) {
        va_start(args, fmt);
        fprintf(state->log_file, "[%s] ", timestamp);
        vfprintf(state->log_file, fmt, args);
        fprintf(state->log_file, "\n");
        fflush(state->log_file);
        va_end(args);
    }
}

static int open_log_file(nm_state_t *state) {
    char path[PATH_MAX];
    if (nm_log_path(path, sizeof(path), state->data_dir, 0) != 0) {
        return -1;
    }

    char logs_dir[PATH_MAX];
    int written = snprintf(logs_dir, sizeof(logs_dir), "%s/logs", state->data_dir);
    if (written < 0 || (size_t)written >= sizeof(logs_dir)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (ensure_dir(logs_dir) != 0) {
        return -1;
    }

    state->log_file = fopen(path, "a");
    return state->log_file == NULL ? -1 : 0;
}

static connection_t *add_connection(nm_state_t *state, int fd) {
    connection_t *conn = calloc(1, sizeof(*conn));
    if (conn == NULL) {
        return NULL;
    }
    conn->fd = fd;
    conn->role = ROLE_PENDING;
    conn->next = state->connections;
    state->connections = conn;
    return conn;
}

static void remove_connection(nm_state_t *state, connection_t *target) {
    if (state == NULL || target == NULL) {
        return;
    }
    connection_t **pp = &state->connections;
    while (*pp != NULL) {
        if (*pp == target) {
            *pp = target->next;
            close(target->fd);
            free(target);
            return;
        }
        pp = &(*pp)->next;
    }
}

static storage_server_info_t *find_storage_server(nm_state_t *state, const char *ss_id) {
    for (storage_server_info_t *info = state->servers; info != NULL; info = info->next) {
        if (strcmp(info->ss_id, ss_id) == 0) {
            return info;
        }
    }
    return NULL;
}

static client_info_t *find_client(nm_state_t *state, const char *username) {
    for (client_info_t *info = state->clients; info != NULL; info = info->next) {
        if (strcmp(info->username, username) == 0) {
            return info;
        }
    }
    return NULL;
}

static storage_server_info_t *select_storage_server_for_record(nm_state_t *state, const file_record_t *record) {
    if (state == NULL || record == NULL || record->ss_count == 0) {
        return NULL;
    }
    storage_server_info_t *best = NULL;
    uint32_t min_load = UINT32_MAX;
    for (size_t i = 0; i < record->ss_count; ++i) {
        storage_server_info_t *candidate = find_storage_server(state, record->ss_ids[i]);
        if (candidate == NULL) {
            continue;
        }
        uint32_t load = candidate->active_readers + candidate->active_writers * 2;
        if (best == NULL || load < min_load) {
            best = candidate;
            min_load = load;
        }
    }
    return best;
}

static int forward_storage_request(storage_server_info_t *ss,
                                   protocol_opcode_t opcode,
                                   uint32_t request_id,
                                   const char *payload,
                                   message_header_t *resp_header_out,
                                   char **resp_payload_out) {
    if (ss == NULL) {
        return -1;
    }

    const char *host = ss->ip[0] != '\0' ? ss->ip : "localhost";
    int ss_fd = tcp_connect(host, ss->client_port);
    if (ss_fd < 0) {
        return -1;
    }

    size_t payload_len = payload != NULL ? strlen(payload) : 0;
    message_header_t req_header = {
        .version = PROTOCOL_VERSION,
        .opcode = opcode,
        .request_id = request_id,
        .payload_len = (uint32_t)payload_len
    };

    uint8_t header_buf[PROTOCOL_HEADER_SIZE];
    if (protocol_encode_header(&req_header, header_buf) != 0) {
        close(ss_fd);
        return -1;
    }
    if (send_all(ss_fd, header_buf, sizeof(header_buf)) < 0) {
        close(ss_fd);
        return -1;
    }
    if (payload_len > 0 && send_all(ss_fd, payload, payload_len) < 0) {
        close(ss_fd);
        return -1;
    }

    if (recv_all(ss_fd, header_buf, sizeof(header_buf)) <= 0) {
        close(ss_fd);
        return -1;
    }

    message_header_t resp_header;
    if (protocol_decode_header(header_buf, &resp_header) != 0) {
        close(ss_fd);
        return -1;
    }

    char *resp_payload = NULL;
    if (resp_header.payload_len > 0) {
        resp_payload = calloc(1, resp_header.payload_len + 1);
        if (resp_payload == NULL) {
            close(ss_fd);
            return -1;
        }
        if (recv_all(ss_fd, resp_payload, resp_header.payload_len) <= 0) {
            free(resp_payload);
            close(ss_fd);
            return -1;
        }
        resp_payload[resp_header.payload_len] = '\0';
    }

    close(ss_fd);
    if (resp_header_out != NULL) {
        *resp_header_out = resp_header;
    }
    if (resp_payload_out != NULL) {
        *resp_payload_out = resp_payload;
    } else if (resp_payload != NULL) {
        free(resp_payload);
    }
    return 0;
}

static bool json_get_string(const char *json, const char *key, char *out, size_t out_len) {
    if (json == NULL || key == NULL || out == NULL || out_len == 0) {
        return false;
    }
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (pos == NULL) {
        return false;
    }
    pos += strlen(pattern);
    pos = strchr(pos, ':');
    if (pos == NULL) {
        return false;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t') {
        pos++;
    }
    if (*pos != '"') {
        return false;
    }
    pos++;
    const char *end = strchr(pos, '"');
    if (end == NULL) {
        return false;
    }
    size_t len = (size_t)(end - pos);
    if (len >= out_len) {
        len = out_len - 1;
    }
    memcpy(out, pos, len);
    out[len] = '\0';
    return true;
}

static bool json_get_uint(const char *json, const char *key, unsigned long *out_value) {
    if (json == NULL || key == NULL || out_value == NULL) {
        return false;
    }
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (pos == NULL) {
        return false;
    }
    pos += strlen(pattern);
    pos = strchr(pos, ':');
    if (pos == NULL) {
        return false;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t') {
        pos++;
    }
    char *endptr = NULL;
    errno = 0;
    unsigned long value = strtoul(pos, &endptr, 10);
    if (pos == endptr || errno != 0) {
        return false;
    }
    *out_value = value;
    return true;
}

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} string_list_t;

static void string_list_free(string_list_t *list) {
    if (list == NULL) {
        return;
    }
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int string_list_add_unique(string_list_t *list, const char *value) {
    if (list == NULL || value == NULL || value[0] == '\0') {
        return 0;
    }
    for (size_t i = 0; i < list->count; ++i) {
        if (strcmp(list->items[i], value) == 0) {
            return 0;
        }
    }
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity ? list->capacity * 2 : 8;
        char **tmp = realloc(list->items, new_cap * sizeof(char *));
        if (tmp == NULL) {
            return -1;
        }
        list->items = tmp;
        list->capacity = new_cap;
    }
    list->items[list->count] = strdup(value);
    if (list->items[list->count] == NULL) {
        return -1;
    }
    list->count++;
    return 0;
}

static void parse_json_string_array(const char *json, const char *key, string_list_t *out_list) {
    if (json == NULL || key == NULL || out_list == NULL) {
        return;
    }
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":[", key);
    const char *start = strstr(json, pattern);
    if (start == NULL) {
        return;
    }
    start += strlen(pattern);
    const char *ptr = start;
    int in_string = 0;
    int escape = 0;
    const char *end = NULL;
    while (*ptr) {
        char ch = *ptr;
        if (in_string) {
            if (escape) {
                escape = 0;
            } else if (ch == '\\') {
                escape = 1;
            } else if (ch == '"') {
                in_string = 0;
            }
        } else {
            if (ch == '"') {
                in_string = 1;
            } else if (ch == ']') {
                end = ptr;
                break;
            }
        }
        ptr++;
    }
    if (end == NULL) {
        return;
    }

    const char *cursor = start;
    in_string = 0;
    escape = 0;
    char buffer[512];
    size_t buf_idx = 0;
    while (cursor < end) {
        char ch = *cursor;
        if (!in_string) {
            if (ch == '"') {
                in_string = 1;
                buf_idx = 0;
            }
            cursor++;
            continue;
        }

        if (escape) {
            if (buf_idx < sizeof(buffer) - 1) {
                buffer[buf_idx++] = ch;
            }
            escape = 0;
            cursor++;
            continue;
        }

        if (ch == '\\') {
            escape = 1;
            cursor++;
            continue;
        }

        if (ch == '"') {
            buffer[buf_idx] = '\0';
            string_list_add_unique(out_list, buffer);
            in_string = 0;
            cursor++;
            continue;
        }

        if (buf_idx < sizeof(buffer) - 1) {
            buffer[buf_idx++] = ch;
        }
        cursor++;
    }
}

static void append_json_string_literal(char *buf, size_t buf_len, size_t *offset, const char *value) {
    if (buf == NULL || offset == NULL || value == NULL || *offset >= buf_len) {
        return;
    }
    if (*offset + 2 >= buf_len) {
        return;
    }
    buf[(*offset)++] = '"';
    for (const char *p = value; *p != '\0' && *offset < buf_len - 2; ++p) {
        char ch = *p;
        if (ch == '"' || ch == '\\') {
            if (*offset + 2 >= buf_len) {
                break;
            }
            buf[(*offset)++] = '\\';
            buf[(*offset)++] = ch;
        } else if (ch == '\n') {
            if (*offset + 2 >= buf_len) {
                break;
            }
            buf[(*offset)++] = '\\';
            buf[(*offset)++] = 'n';
        } else if (ch == '\r') {
            if (*offset + 2 >= buf_len) {
                break;
            }
            buf[(*offset)++] = '\\';
            buf[(*offset)++] = 'r';
        } else if (ch == '\t') {
            if (*offset + 2 >= buf_len) {
                break;
            }
            buf[(*offset)++] = '\\';
            buf[(*offset)++] = 't';
        } else {
            buf[(*offset)++] = ch;
        }
    }
    if (*offset < buf_len - 1) {
        buf[(*offset)++] = '"';
    }
    buf[*offset] = '\0';
}

static void append_registry_entry(nm_state_t *state, const char *entry) {
    char path[PATH_MAX];
    if (ns_registry_path(path, sizeof(path), state->data_dir) != 0) {
        return;
    }
    FILE *fp = fopen(path, "a");
    if (fp == NULL) {
        return;
    }
    fprintf(fp, "%s\n", entry);
    fclose(fp);
}

static void send_error(int fd, uint32_t request_id, protocol_error_t error_code, const char *message) {
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"code\":\"%s\",\"message\":\"%s\"}",
             protocol_error_name(error_code),
             message != NULL ? message : "");

    message_header_t header = {
        .version = PROTOCOL_VERSION,
        .opcode = OP_ERROR,
        .request_id = request_id,
        .payload_len = (uint32_t)strlen(payload)
    };

    uint8_t header_buf[PROTOCOL_HEADER_SIZE];
    if (protocol_encode_header(&header, header_buf) != 0) {
        return;
    }
    send_all(fd, header_buf, sizeof(header_buf));
    send_all(fd, payload, header.payload_len);
}

static void send_register_ack(int fd, uint32_t request_id, nm_state_t *state, connection_t *conn) {
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"status\":\"OK\",\"session_token\":\"%s\",\"heartbeat_interval_ms\":%u}",
             conn->session_token,
             PROTOCOL_HEARTBEAT_INTERVAL_MS);

    message_header_t header = {
        .version = PROTOCOL_VERSION,
        .opcode = OP_REGISTER_ACK,
        .request_id = request_id,
        .payload_len = (uint32_t)strlen(payload)
    };

    uint8_t header_buf[PROTOCOL_HEADER_SIZE];
    if (protocol_encode_header(&header, header_buf) != 0) {
        return;
    }
    send_all(fd, header_buf, sizeof(header_buf));
    send_all(fd, payload, header.payload_len);
    nm_log(state, "Sent register ACK to %s (%s)",
           conn->identifier[0] ? conn->identifier : "pending",
           protocol_opcode_name(conn->role == ROLE_STORAGE_SERVER ? OP_REGISTER_SS : OP_REGISTER_CLIENT));
}

static void handle_register_ss(nm_state_t *state, connection_t *conn, uint32_t request_id, const char *payload) {
    char ss_id[128];
    char ip[64];
    unsigned long port = 0;

    if (!json_get_string(payload, "ss_id", ss_id, sizeof(ss_id))) {
        send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing ss_id");
        return;
    }
    if (!json_get_string(payload, "nm_ip", ip, sizeof(ip))) {
        strncpy(ip, "0.0.0.0", sizeof(ip));
    }
    if (!json_get_uint(payload, "client_port", &port)) {
        send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing client_port");
        return;
    }

    storage_server_info_t *info = find_storage_server(state, ss_id);
    if (info == NULL) {
        info = calloc(1, sizeof(*info));
        if (info == NULL) {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Out of memory");
            return;
        }
        strncpy(info->ss_id, ss_id, sizeof(info->ss_id) - 1);
        info->next = state->servers;
        state->servers = info;
        char entry[256];
        snprintf(entry, sizeof(entry),
                 "{\"type\":\"SS_REGISTER\",\"ss_id\":\"%s\",\"port\":%lu}",
                 ss_id, port);
        append_registry_entry(state, entry);
    }

    strncpy(info->ip, ip, sizeof(info->ip) - 1);
    info->client_port = (uint16_t)port;
    info->last_heartbeat = time(NULL);

    conn->role = ROLE_STORAGE_SERVER;
    strncpy(conn->identifier, ss_id, sizeof(conn->identifier) - 1);

    state->session_seq++;
    snprintf(conn->session_token, sizeof(conn->session_token), "ss-%06lu", state->session_seq);

    const char *manifest_start = strstr(payload, "\"file_manifest\"");
    if (manifest_start != NULL) {
        manifest_start = strchr(manifest_start, '[');
        if (manifest_start != NULL) {
            manifest_start++;
            const char *manifest_end = strchr(manifest_start, ']');
            if (manifest_end != NULL) {
                char manifest_buf[4096];
                size_t len = (size_t)(manifest_end - manifest_start);
                if (len < sizeof(manifest_buf)) {
                    memcpy(manifest_buf, manifest_start, len);
                    manifest_buf[len] = '\0';
                    
                    char *pos = manifest_buf;
                    while (*pos) {
                        while (*pos == ' ' || *pos == '\t' || *pos == ',' || *pos == '"') pos++;
                        if (*pos == '\0') break;
                        char filename[256];
                        size_t i = 0;
                        while (*pos && *pos != '"' && *pos != ',' && i < sizeof(filename) - 1) {
                            filename[i++] = *pos++;
                        }
                        filename[i] = '\0';
                        if (i > 0) {
                            file_index_add(state->file_index, filename, "system", ss_id);
                            nm_log(state, "Registered file '%s' on SS %s", filename, ss_id);
                        }
                    }
                }
            }
        }
    }

    nm_log(state, "Registered storage server %s (client port %u)", ss_id, info->client_port);
    
    /* Send registration ACK FIRST, before recovery operations */
    send_register_ack(conn->fd, request_id, state, conn);
    
    /* SS Recovery: Check if this SS needs to sync any files (after ACK) */
    size_t total_files = 0;
    file_record_t **all_files = file_index_get_all(state->file_index, &total_files);
    if (all_files != NULL) {
        time_t now = time(NULL);
        int recovery_count = 0;
        for (size_t i = 0; i < total_files; i++) {
            file_record_t *rec = all_files[i];
            int is_replica = 0;
            for (size_t j = 0; j < rec->ss_count; j++) {
                if (strcmp(rec->ss_ids[j], ss_id) == 0) {
                    is_replica = 1;
                    break;
                }
            }
            
            if (is_replica) {
                /* Find a source to copy from */
                storage_server_info_t *source_ss = NULL;
                for (size_t j = 0; j < rec->ss_count; j++) {
                    if (strcmp(rec->ss_ids[j], ss_id) != 0) {
                        storage_server_info_t *candidate = find_storage_server(state, rec->ss_ids[j]);
                        if (candidate != NULL && (now - candidate->last_heartbeat <= 15)) {
                            source_ss = candidate;
                            break;
                        }
                    }
                }
                
                if (source_ss != NULL) {
                    /* Instruct SS to replicate from source_ss */
                    char repl_payload[512];
                    snprintf(repl_payload, sizeof(repl_payload),
                             "{\"filename\":\"%s\",\"source_ip\":\"%s\",\"source_port\":%u}",
                             rec->filename, source_ss->ip, source_ss->client_port);
                    
                    message_header_t repl_hdr = {
                        .version = PROTOCOL_VERSION,
                        .opcode = OP_REPLICATE_FILE,
                        .request_id = 0,
                        .payload_len = (uint32_t)strlen(repl_payload)
                    };
                    
                    uint8_t repl_buf[PROTOCOL_HEADER_SIZE];
                    protocol_encode_header(&repl_hdr, repl_buf);
                    send_all(conn->fd, repl_buf, sizeof(repl_buf));
                    send_all(conn->fd, repl_payload, repl_hdr.payload_len);
                    
                    recovery_count++;
                    nm_log(state, "Recovery: Instructed SS %s to replicate '%s' from %s", 
                           ss_id, rec->filename, source_ss->ss_id);
                }
            }
        }
        if (recovery_count > 0) {
            nm_log(state, "Recovery: Sent %d file replication requests to SS %s", recovery_count, ss_id);
        }
        free(all_files);
    }
}

static void handle_register_client(nm_state_t *state, connection_t *conn, uint32_t request_id, const char *payload) {
    char username[128];
    char ip[64];
    unsigned long port = 0;

    if (!json_get_string(payload, "username", username, sizeof(username))) {
        send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing username");
        return;
    }
    if (!json_get_string(payload, "client_ip", ip, sizeof(ip))) {
        strncpy(ip, "0.0.0.0", sizeof(ip));
    }
    if (!json_get_uint(payload, "client_port", &port)) {
        port = 0;
    }

    client_info_t *info = find_client(state, username);
    if (info == NULL) {
        info = calloc(1, sizeof(*info));
        if (info == NULL) {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Out of memory");
            return;
        }
        strncpy(info->username, username, sizeof(info->username) - 1);
        info->next = state->clients;
        state->clients = info;
        char entry[256];
        snprintf(entry, sizeof(entry),
                 "{\"type\":\"CLIENT_REGISTER\",\"username\":\"%s\"}",
                 username);
        append_registry_entry(state, entry);
    }

    strncpy(info->ip, ip, sizeof(info->ip) - 1);
    info->client_port = (uint16_t)port;
    info->registered_at = time(NULL);

    conn->role = ROLE_CLIENT;
    strncpy(conn->identifier, username, sizeof(conn->identifier) - 1);

    state->session_seq++;
    snprintf(conn->session_token, sizeof(conn->session_token), "client-%06lu", state->session_seq);

    nm_log(state, "Registered client %s", username);
    send_register_ack(conn->fd, request_id, state, conn);
}

static void handle_heartbeat(nm_state_t *state, connection_t *conn, uint32_t request_id, const char *payload) {
    (void)request_id;
    char ss_id[128];
    if (!json_get_string(payload, "ss_id", ss_id, sizeof(ss_id))) {
        nm_log(state, "Heartbeat missing ss_id");
        return;
    }
    storage_server_info_t *info = find_storage_server(state, ss_id);
    if (info == NULL) {
        nm_log(state, "Heartbeat from unknown SS %s", ss_id);
        return;
    }
    unsigned long readers = 0;
    unsigned long writers = 0;
    if (json_get_uint(payload, "readers", &readers)) {
        info->active_readers = (uint32_t)readers;
    }
    if (json_get_uint(payload, "writers", &writers)) {
        info->active_writers = (uint32_t)writers;
    }
    info->last_heartbeat = time(NULL);
    (void)conn;
}

static void handle_lookup_file(nm_state_t *state, connection_t *conn, uint32_t request_id, const char *payload) {
    char filename[MAX_FILENAME_LEN];
    char username[MAX_USERNAME_LEN];
    
    if (!json_get_string(payload, "filename", filename, sizeof(filename))) {
        send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing filename");
        return;
    }
    if (!json_get_string(payload, "username", username, sizeof(username))) {
        strncpy(username, "anonymous", sizeof(username));
    }
    
    nm_log(state, "Lookup request for '%s' by user '%s'", filename, username);
    
    file_record_t *record = lru_cache_get(state->cache, state->file_index, filename);
    if (record == NULL) {
        nm_log(state, "File '%s' not found", filename);
        send_error(conn->fd, request_id, ERR_FILE_NOT_FOUND, "File not found");
        return;
    }
    
    int need_write = 0;
    const char *operation = strstr(payload, "\"operation\"");
    if (operation != NULL) {
        if (strstr(operation, "write") != NULL || strstr(operation, "WRITE") != NULL) {
            need_write = 1;
        }
    }
    
    if (!file_record_check_access(record, username, need_write)) {
        nm_log(state, "Access denied for user '%s' to file '%s'", username, filename);
        send_error(conn->fd, request_id, ERR_NO_ACCESS, "Access denied");
        return;
    }
    
    if (record->ss_count == 0) {
        nm_log(state, "No storage servers for file '%s'", filename);
        send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "No storage servers available");
        return;
    }
    
    size_t best_idx = 0;
    uint32_t min_load = UINT32_MAX;
    int found_active = 0;
    time_t now = time(NULL);

    for (size_t i = 0; i < record->ss_count; ++i) {
        storage_server_info_t *ss_info = find_storage_server(state, record->ss_ids[i]);
        if (ss_info != NULL) {
            /* Failure Detection: Check if SS is alive (15s threshold) */
            if (now - ss_info->last_heartbeat > 15) {
                nm_log(state, "Skipping dead SS %s (last heartbeat %lds ago)", 
                       ss_info->ss_id, now - ss_info->last_heartbeat);
                continue;
            }

            uint32_t load = ss_info->active_readers + ss_info->active_writers * 2;
            if (load < min_load) {
                min_load = load;
                best_idx = i;
                found_active = 1;
            }
        }
    }
    
    if (!found_active) {
        nm_log(state, "No active storage servers for file '%s'", filename);
        send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "All storage servers down");
        return;
    }
    
    storage_server_info_t *chosen_ss = find_storage_server(state, record->ss_ids[best_idx]);
    if (chosen_ss == NULL) {
        send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "Storage server unavailable");
        return;
    }
    
    /* Construct replicas list for client-side replication */
    char replicas_json[1024] = "[";
    int first_replica = 1;
    for (size_t i = 0; i < record->ss_count; ++i) {
        if (i == best_idx) continue; /* Skip primary */
        
        storage_server_info_t *ss_info = find_storage_server(state, record->ss_ids[i]);
        if (ss_info != NULL && now - ss_info->last_heartbeat <= 15) {
            if (!first_replica) strcat(replicas_json, ",");
            char entry[128];
            snprintf(entry, sizeof(entry), "{\"ip\":\"%s\",\"port\":%u}", 
                     ss_info->ip, ss_info->client_port);
            strcat(replicas_json, entry);
            first_replica = 0;
        }
    }
    strcat(replicas_json, "]");
    
    char response[2048];
    snprintf(response, sizeof(response),
             "{\"filename\":\"%s\",\"ss_ip\":\"%s\",\"ss_port\":%u,\"owner\":\"%s\",\"replicas\":%s}",
             filename, chosen_ss->ip, chosen_ss->client_port, record->owner, replicas_json);
    
    message_header_t header = {
        .version = PROTOCOL_VERSION,
        .opcode = OP_LOOKUP_RESP,
        .request_id = request_id,
        .payload_len = (uint32_t)strlen(response)
    };
    
    uint8_t header_buf[PROTOCOL_HEADER_SIZE];
    if (protocol_encode_header(&header, header_buf) == 0) {
        send_all(conn->fd, header_buf, sizeof(header_buf));
        send_all(conn->fd, response, header.payload_len);
        nm_log(state, "Sent lookup response for '%s' -> SS %s (%s:%u) + replicas",
               filename, record->ss_ids[best_idx], chosen_ss->ip, chosen_ss->client_port);
    }
}

static void handle_command_forward(nm_state_t *state, connection_t *conn, uint32_t request_id, const char *payload) {
    char filename[MAX_FILENAME_LEN] = {0};
    char username[MAX_USERNAME_LEN] = {0};
    char command[64] = {0};
    
    /* Command is required */
    if (!json_get_string(payload, "command", command, sizeof(command))) {
        send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing command");
        return;
    }
    
    /* Username is required */
    if (!json_get_string(payload, "username", username, sizeof(username))) {
        strncpy(username, "anonymous", sizeof(username));
    }
    
    /* Filename is optional (not needed for VIEW, LIST) */
    json_get_string(payload, "filename", filename, sizeof(filename));
    
    nm_log(state, "Command forward: %s on '%s' by '%s'", command, 
           filename[0] ? filename : "(no file)", username);
    
    if (strcmp(command, "CREATE") == 0) {
        file_record_t *existing = file_index_lookup(state->file_index, filename);
        if (existing != NULL) {
            send_error(conn->fd, request_id, ERR_DUPLICATE, "File already exists");
            return;
        }
        
        storage_server_info_t *primary_ss = NULL;
        storage_server_info_t *replica_ss = NULL;
        
        /* Collect all active storage servers */
        storage_server_info_t *active_servers[16];
        int active_count = 0;
        storage_server_info_t *curr = state->servers;
        time_t now = time(NULL);
        
        while (curr != NULL && active_count < 16) {
            /* Check if SS is alive (heartbeat within 15s) */
            if (now - curr->last_heartbeat <= 15) {
                active_servers[active_count++] = curr;
            }
            curr = curr->next;
        }
        
        if (active_count == 0) {
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "No active storage servers available");
            return;
        }
        
        /* Select primary using round-robin */
        primary_ss = active_servers[state->next_primary_index % active_count];
        state->next_primary_index++;
        
        /* Select replica (different from primary) */
        if (active_count > 1) {
            /* Pick the next server in round-robin order */
            replica_ss = active_servers[state->next_primary_index % active_count];
            state->next_primary_index++;
        }
        
        /* 1. Create on Primary SS */
        int ss_fd = tcp_connect("localhost", primary_ss->client_port);
        if (ss_fd < 0) {
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "Failed to connect to storage server");
            return;
        }
        
        char create_payload[512];
        snprintf(create_payload, sizeof(create_payload),
                 "{\"filename\":\"%s\",\"owner\":\"%s\"}",
                 filename, username);
        
        message_header_t ss_req = {
            .version = PROTOCOL_VERSION,
            .opcode = OP_COMMAND_FORWARD,
            .request_id = request_id,
            .payload_len = (uint32_t)strlen(create_payload)
        };
        
        uint8_t ss_header_buf[PROTOCOL_HEADER_SIZE];
        protocol_encode_header(&ss_req, ss_header_buf);
        send_all(ss_fd, ss_header_buf, sizeof(ss_header_buf));
        send_all(ss_fd, create_payload, ss_req.payload_len);
        
        recv_all(ss_fd, ss_header_buf, sizeof(ss_header_buf));
        message_header_t ss_resp;
        protocol_decode_header(ss_header_buf, &ss_resp);
        
        char *ss_resp_payload = NULL;
        if (ss_resp.payload_len > 0) {
            ss_resp_payload = calloc(1, ss_resp.payload_len + 1);
            if (ss_resp_payload) {
                recv_all(ss_fd, ss_resp_payload, ss_resp.payload_len);
            }
        }
        close(ss_fd);
        
        if (ss_resp.opcode != OP_COMMAND_STATUS) {
            if (ss_resp_payload) {
                protocol_encode_header(&ss_resp, ss_header_buf);
                send_all(conn->fd, ss_header_buf, sizeof(ss_header_buf));
                send_all(conn->fd, ss_resp_payload, ss_resp.payload_len);
                free(ss_resp_payload);
            } else {
                send_error(conn->fd, request_id, ERR_EXECUTION_FAIL, "Storage server failed to create file");
            }
            return;
        }
        free(ss_resp_payload);
        
        /* 2. Create on Replica SS (Async/Best-effort) */
        if (replica_ss != NULL) {
            int rep_fd = tcp_connect("localhost", replica_ss->client_port);
            if (rep_fd >= 0) {
                /* Re-encode the original request header (not the response header) */
                uint8_t rep_header_buf[PROTOCOL_HEADER_SIZE];
                protocol_encode_header(&ss_req, rep_header_buf);
                send_all(rep_fd, rep_header_buf, sizeof(rep_header_buf));
                send_all(rep_fd, create_payload, ss_req.payload_len);
                
                /* Drain the response to avoid TCP RST, but don't wait if it blocks */
                /* Use non-blocking read with short timeout */
                struct timeval tv = {.tv_sec = 0, .tv_usec = 100000}; /* 100ms */
                setsockopt(rep_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                
                uint8_t discard_buf[PROTOCOL_HEADER_SIZE + 512];
                recv(rep_fd, discard_buf, sizeof(discard_buf), 0);
                
                close(rep_fd);
                nm_log(state, "Replicated creation of '%s' to SS %s", filename, replica_ss->ss_id);
            }
        }
        
        /* Register in Name Server */
        file_index_add(state->file_index, filename, username, primary_ss->ss_id);
        if (replica_ss != NULL) {
            file_index_add_ss_replica(state->file_index, filename, replica_ss->ss_id);
        }
        lru_cache_invalidate(state->cache, filename);
        
        /* Create initial ACL file with owner having RW access */
        char acl_path[PATH_MAX];
        if (ns_acl_path(acl_path, sizeof(acl_path), state->data_dir, filename) == 0) {
            FILE *acl_file = fopen(acl_path, "w");
            if (acl_file != NULL) {
                fprintf(acl_file, "%s:RW\n", username);
                fclose(acl_file);
            }
        }
        
        /* Add file entry to registry for persistence */
        char file_entry[1024];
        snprintf(file_entry, sizeof(file_entry),
                 "{\"type\":\"FILE_CREATE\",\"filename\":\"%s\",\"owner\":\"%s\","
                 "\"ss_id\":\"%s\",\"replica_ss_id\":\"%s\",\"created_at\":%ld,\"modified_at\":%ld,"
                 "\"word_count\":0,\"char_count\":0,\"sentence_count\":0}",
                 filename, username, primary_ss->ss_id, 
                 replica_ss ? replica_ss->ss_id : "", now, now);
        append_registry_entry(state, file_entry);
        
        char response[512];
        snprintf(response, sizeof(response),
                 "{\"status\":\"OK\",\"message\":\"File created\",\"ss_id\":\"%s\",\"replica\":\"%s\"}",
                 primary_ss->ss_id, replica_ss ? replica_ss->ss_id : "none");
        
        message_header_t header = {
            .version = PROTOCOL_VERSION,
            .opcode = OP_COMMAND_STATUS,
            .request_id = request_id,
            .payload_len = (uint32_t)strlen(response)
        };
        
        uint8_t header_buf[PROTOCOL_HEADER_SIZE];
        if (protocol_encode_header(&header, header_buf) == 0) {
            send_all(conn->fd, header_buf, sizeof(header_buf));
            send_all(conn->fd, response, header.payload_len);
        }
        nm_log(state, "Created file '%s' owned by '%s' on SS %s (Replica: %s)", 
               filename, username, primary_ss->ss_id, replica_ss ? replica_ss->ss_id : "none");
        
    } else if (strcmp(command, "DELETE") == 0) {
        file_record_t *record = file_index_lookup(state->file_index, filename);
        if (record == NULL) {
            send_error(conn->fd, request_id, ERR_FILE_NOT_FOUND, "File not found");
            return;
        }
        
        if (strcmp(record->owner, username) != 0) {
            send_error(conn->fd, request_id, ERR_NOT_OWNER, "Only owner can delete");
            return;
        }
        
        /* Send delete requests to all storage servers that have this file */
        for (size_t i = 0; i < record->ss_count; i++) {
            storage_server_info_t *ss = find_storage_server(state, record->ss_ids[i]);
            if (ss != NULL) {
                int ss_fd = tcp_connect("localhost", ss->client_port);
                if (ss_fd >= 0) {
                    char delete_req[512];
                    snprintf(delete_req, sizeof(delete_req), "{\"filename\":\"%s\"}", filename);
                    
                    message_header_t req_hdr = {
                        .version = PROTOCOL_VERSION,
                        .opcode = OP_DELETE_REQUEST,
                        .request_id = 0,
                        .payload_len = (uint32_t)strlen(delete_req)
                    };
                    
                    uint8_t req_buf[PROTOCOL_HEADER_SIZE];
                    if (protocol_encode_header(&req_hdr, req_buf) == 0) {
                        send_all(ss_fd, req_buf, sizeof(req_buf));
                        send_all(ss_fd, delete_req, req_hdr.payload_len);
                        
                        /* Wait for response (optional - we'll proceed anyway) */
                        uint8_t resp_buf[PROTOCOL_HEADER_SIZE];
                        recv_all(ss_fd, resp_buf, sizeof(resp_buf));
                    }
                    close(ss_fd);
                    nm_log(state, "Sent delete request for '%s' to SS %s", filename, record->ss_ids[i]);
                }
            }
        }
        
        /* Remove from name server's index and cache */
        file_index_remove(state->file_index, filename);
        lru_cache_invalidate(state->cache, filename);
        
        /* Also delete the ACL file */
        char acl_path[PATH_MAX];
        int path_len = snprintf(acl_path, sizeof(acl_path), "%s/acl/%s.acl.json", state->data_dir, filename);
        if (path_len > 0 && (size_t)path_len < sizeof(acl_path)) {
            unlink(acl_path);
        }
        char req_path[PATH_MAX];
        if (ns_requests_path(req_path, sizeof(req_path), state->data_dir, filename) == 0) {
            unlink(req_path);
        }
        
        char response[256];
        snprintf(response, sizeof(response),
                 "{\"status\":\"OK\",\"message\":\"File deleted\"}");
        
        message_header_t header = {
            .version = PROTOCOL_VERSION,
            .opcode = OP_COMMAND_STATUS,
            .request_id = request_id,
            .payload_len = (uint32_t)strlen(response)
        };
        
        uint8_t header_buf[PROTOCOL_HEADER_SIZE];
        if (protocol_encode_header(&header, header_buf) == 0) {
            send_all(conn->fd, header_buf, sizeof(header_buf));
            send_all(conn->fd, response, header.payload_len);
        }
        nm_log(state, "Deleted file '%s' by owner '%s'", filename, username);
        
    } else if (strcmp(command, "ADDACCESS") == 0) {
        file_record_t *record = file_index_lookup(state->file_index, filename);
        if (record == NULL) {
            send_error(conn->fd, request_id, ERR_FILE_NOT_FOUND, "File not found");
            return;
        }
        
        if (strcmp(record->owner, username) != 0) {
            send_error(conn->fd, request_id, ERR_NOT_OWNER, "Only owner can modify access");
            return;
        }
        
        char target_user[MAX_USERNAME_LEN];
        if (!json_get_string(payload, "target_user", target_user, sizeof(target_user))) {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing target_user");
            return;
        }
        
        /* Check if target user is registered */
        if (find_client(state, target_user) == NULL) {
            send_error(conn->fd, request_id, ERR_UNREGISTERED_USER, "User not registered");
            return;
        }
        
        int can_write = 0;
        const char *mode = strstr(payload, "\"mode\"");
        if (mode != NULL && strstr(mode, "write") != NULL) {
            can_write = 1;
        }
        
        file_record_add_acl(record, target_user, 1, can_write);
        lru_cache_invalidate(state->cache, filename);
        persist_acl_file(state, record);
        
        char response[256];
        snprintf(response, sizeof(response),
                 "{\"status\":\"OK\",\"message\":\"Access granted\"}");
        
        message_header_t header = {
            .version = PROTOCOL_VERSION,
            .opcode = OP_COMMAND_STATUS,
            .request_id = request_id,
            .payload_len = (uint32_t)strlen(response)
        };
        
        uint8_t header_buf[PROTOCOL_HEADER_SIZE];
        if (protocol_encode_header(&header, header_buf) == 0) {
            send_all(conn->fd, header_buf, sizeof(header_buf));
            send_all(conn->fd, response, header.payload_len);
        }
        nm_log(state, "Granted %s access to '%s' for user '%s'",
               can_write ? "write" : "read", filename, target_user);
        
    } else if (strcmp(command, "REMACCESS") == 0) {
        file_record_t *record = file_index_lookup(state->file_index, filename);
        if (record == NULL) {
            send_error(conn->fd, request_id, ERR_FILE_NOT_FOUND, "File not found");
            return;
        }
        
        if (strcmp(record->owner, username) != 0) {
            send_error(conn->fd, request_id, ERR_NOT_OWNER, "Only owner can modify access");
            return;
        }
        
        char target_user[MAX_USERNAME_LEN];
        if (!json_get_string(payload, "target_user", target_user, sizeof(target_user))) {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing target_user");
            return;
        }
        
        file_record_remove_acl(record, target_user);
        lru_cache_invalidate(state->cache, filename);
        persist_acl_file(state, record);
        
        char response[256];
        snprintf(response, sizeof(response),
                 "{\"status\":\"OK\",\"message\":\"Access removed\"}");
        
        message_header_t header = {
            .version = PROTOCOL_VERSION,
            .opcode = OP_COMMAND_STATUS,
            .request_id = request_id,
            .payload_len = (uint32_t)strlen(response)
        };
        
        uint8_t header_buf[PROTOCOL_HEADER_SIZE];
        if (protocol_encode_header(&header, header_buf) == 0) {
            send_all(conn->fd, header_buf, sizeof(header_buf));
            send_all(conn->fd, response, header.payload_len);
        }
        nm_log(state, "Removed access to '%s' for user '%s'", filename, target_user);
        
    } else if (strcmp(command, "REQUESTACCESS") == 0) {
        if (filename[0] == '\0') {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing filename");
            return;
        }
        file_record_t *record = file_index_lookup(state->file_index, filename);
        if (record == NULL) {
            send_error(conn->fd, request_id, ERR_FILE_NOT_FOUND, "File not found");
            return;
        }
        if (strcmp(record->owner, username) == 0) {
            send_error(conn->fd, request_id, ERR_DUPLICATE, "You already own this file");
            return;
        }
        char mode_value[16] = {0};
        json_get_string(payload, "mode", mode_value, sizeof(mode_value));
        int can_write_req = 0;
        if (parse_access_mode(mode_value, &can_write_req) != 0) {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Mode must be 'read' or 'write'");
            return;
        }
        if (file_record_check_access(record, username, can_write_req)) {
            send_error(conn->fd, request_id, ERR_DUPLICATE,
                       can_write_req ? "You already have write access" : "You already have access");
            return;
        }
        access_request_t *requests = NULL;
        size_t req_count = 0;
        if (load_access_requests(state, filename, &requests, &req_count) != 0) {
            free(requests);
            send_error(conn->fd, request_id, ERR_EXECUTION_FAIL, "Failed to load existing requests");
            return;
        }
        int idx = find_request_by_user(requests, req_count, username);
        if (idx < 0) {
            access_request_t *tmp = realloc(requests, (req_count + 1) * sizeof(*requests));
            if (tmp == NULL) {
                free(requests);
                send_error(conn->fd, request_id, ERR_EXECUTION_FAIL, "Out of memory");
                return;
            }
            requests = tmp;
            idx = (int)req_count;
            req_count++;
        }
        access_request_t *entry = &requests[idx];
        strncpy(entry->username, username, sizeof(entry->username) - 1);
        entry->username[sizeof(entry->username) - 1] = '\0';
        entry->can_write = can_write_req;
        entry->requested_at = time(NULL);
        if (save_access_requests(state, filename, requests, req_count) != 0) {
            free(requests);
            send_error(conn->fd, request_id, ERR_EXECUTION_FAIL, "Failed to store request");
            return;
        }
        free(requests);
        char response[256];
        snprintf(response, sizeof(response),
                 "{\"status\":\"pending\",\"mode\":\"%s\"}",
                 can_write_req ? "write" : "read");
        message_header_t header = {
            .version = PROTOCOL_VERSION,
            .opcode = OP_COMMAND_STATUS,
            .request_id = request_id,
            .payload_len = (uint32_t)strlen(response)
        };
        uint8_t header_buf[PROTOCOL_HEADER_SIZE];
        if (protocol_encode_header(&header, header_buf) == 0) {
            send_all(conn->fd, header_buf, sizeof(header_buf));
            send_all(conn->fd, response, header.payload_len);
        }
        nm_log(state, "Queued %s request for '%s' by '%s'",
               can_write_req ? "write" : "read", filename, username);
        
    } else if (strcmp(command, "LISTREQUESTS") == 0) {
        char response[8192] = {0};
        size_t offset = 0;
        int total = 0;
        if (filename[0] != '\0') {
            file_record_t *record = file_index_lookup(state->file_index, filename);
            if (record == NULL) {
                send_error(conn->fd, request_id, ERR_FILE_NOT_FOUND, "File not found");
                return;
            }
            if (strcmp(record->owner, username) != 0) {
                send_error(conn->fd, request_id, ERR_NOT_OWNER, "Only the owner can view requests for this file");
                return;
            }
            access_request_t *requests = NULL;
            size_t req_count = 0;
            if (load_access_requests(state, filename, &requests, &req_count) != 0) {
                free(requests);
                send_error(conn->fd, request_id, ERR_EXECUTION_FAIL, "Failed to load requests");
                return;
            }
            if (req_count == 0) {
                snprintf(response, sizeof(response), "No pending requests for %s", filename);
            } else {
                for (size_t i = 0; i < req_count && offset < sizeof(response) - 64; i++) {
                    char time_buf[32];
                    format_time_string(requests[i].requested_at, time_buf, sizeof(time_buf));
                    offset += snprintf(response + offset, sizeof(response) - offset,
                                       "%s -> %s (%s) @ %s\n",
                                       filename,
                                       requests[i].username,
                                       requests[i].can_write ? "write" : "read",
                                       time_buf);
                    total++;
                }
            }
            free(requests);
        } else {
            size_t file_count = 0;
            file_record_t **all_files = file_index_get_all(state->file_index, &file_count);
            if (all_files != NULL) {
                for (size_t i = 0; i < file_count && offset < sizeof(response) - 64; i++) {
                    file_record_t *record = all_files[i];
                    if (record == NULL || strcmp(record->owner, username) != 0) {
                        continue;
                    }
                    access_request_t *requests = NULL;
                    size_t req_count = 0;
                    if (load_access_requests(state, record->filename, &requests, &req_count) != 0) {
                        free(requests);
                        continue;
                    }
                    for (size_t j = 0; j < req_count && offset < sizeof(response) - 64; j++) {
                        char time_buf[32];
                        format_time_string(requests[j].requested_at, time_buf, sizeof(time_buf));
                        offset += snprintf(response + offset, sizeof(response) - offset,
                                           "%s -> %s (%s) @ %s\n",
                                           record->filename,
                                           requests[j].username,
                                           requests[j].can_write ? "write" : "read",
                                           time_buf);
                        total++;
                    }
                    free(requests);
                }
                free(all_files);
            }
            if (total == 0) {
                snprintf(response, sizeof(response), "No pending requests");
            }
        }
        message_header_t header = {
            .version = PROTOCOL_VERSION,
            .opcode = OP_COMMAND_STATUS,
            .request_id = request_id,
            .payload_len = (uint32_t)strlen(response)
        };
        uint8_t header_buf[PROTOCOL_HEADER_SIZE];
        if (protocol_encode_header(&header, header_buf) == 0) {
            send_all(conn->fd, header_buf, sizeof(header_buf));
            send_all(conn->fd, response, header.payload_len);
        }
        nm_log(state, "Listed %d pending requests for '%s'",
               total, filename[0] ? filename : username);
        
    } else if (strcmp(command, "APPROVEREQUEST") == 0) {
        if (filename[0] == '\0') {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing filename");
            return;
        }
        char target_user[MAX_USERNAME_LEN];
        if (!json_get_string(payload, "target_user", target_user, sizeof(target_user))) {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing target_user");
            return;
        }
        char mode_value[16] = {0};
        json_get_string(payload, "mode", mode_value, sizeof(mode_value));
        int can_write_req = 0;
        if (parse_access_mode(mode_value, &can_write_req) != 0) {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Mode must be 'read' or 'write'");
            return;
        }
        file_record_t *record = file_index_lookup(state->file_index, filename);
        if (record == NULL) {
            send_error(conn->fd, request_id, ERR_FILE_NOT_FOUND, "File not found");
            return;
        }
        if (strcmp(record->owner, username) != 0) {
            send_error(conn->fd, request_id, ERR_NOT_OWNER, "Only the owner can approve requests");
            return;
        }
        access_request_t *requests = NULL;
        size_t req_count = 0;
        if (load_access_requests(state, filename, &requests, &req_count) != 0) {
            free(requests);
            send_error(conn->fd, request_id, ERR_EXECUTION_FAIL, "Failed to load requests");
            return;
        }
        int idx = find_request_index(requests, req_count, target_user, can_write_req);
        if (idx < 0) {
            free(requests);
            send_error(conn->fd, request_id, ERR_NO_REQUEST, "No matching request found");
            return;
        }
        int can_read = 1;
        int granted_write = can_write_req ? 1 : 0;
        if (file_record_add_acl(record, target_user, can_read, granted_write) != 0) {
            free(requests);
            send_error(conn->fd, request_id, ERR_EXECUTION_FAIL, "Failed to update ACL");
            return;
        }
        persist_acl_file(state, record);
        lru_cache_invalidate(state->cache, filename);
        remove_request_at(requests, &req_count, (size_t)idx);
        if (save_access_requests(state, filename, requests, req_count) != 0) {
            free(requests);
            send_error(conn->fd, request_id, ERR_EXECUTION_FAIL, "Failed to persist updated requests");
            return;
        }
        free(requests);
        char response[256];
        snprintf(response, sizeof(response),
                 "{\"status\":\"approved\",\"user\":\"%s\",\"mode\":\"%s\"}",
                 target_user,
                 granted_write ? "write" : "read");
        message_header_t header = {
            .version = PROTOCOL_VERSION,
            .opcode = OP_COMMAND_STATUS,
            .request_id = request_id,
            .payload_len = (uint32_t)strlen(response)
        };
        uint8_t header_buf[PROTOCOL_HEADER_SIZE];
        if (protocol_encode_header(&header, header_buf) == 0) {
            send_all(conn->fd, header_buf, sizeof(header_buf));
            send_all(conn->fd, response, header.payload_len);
        }
        nm_log(state, "Approved %s access to '%s' for '%s'",
               granted_write ? "write" : "read", filename, target_user);
        
    } else if (strcmp(command, "DENYREQUEST") == 0) {
        if (filename[0] == '\0') {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing filename");
            return;
        }
        char target_user[MAX_USERNAME_LEN];
        if (!json_get_string(payload, "target_user", target_user, sizeof(target_user))) {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing target_user");
            return;
        }
        char mode_value[16] = {0};
        json_get_string(payload, "mode", mode_value, sizeof(mode_value));
        int can_write_req = 0;
        if (parse_access_mode(mode_value, &can_write_req) != 0) {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Mode must be 'read' or 'write'");
            return;
        }
        file_record_t *record = file_index_lookup(state->file_index, filename);
        if (record == NULL) {
            send_error(conn->fd, request_id, ERR_FILE_NOT_FOUND, "File not found");
            return;
        }
        if (strcmp(record->owner, username) != 0) {
            send_error(conn->fd, request_id, ERR_NOT_OWNER, "Only the owner can deny requests");
            return;
        }
        access_request_t *requests = NULL;
        size_t req_count = 0;
        if (load_access_requests(state, filename, &requests, &req_count) != 0) {
            free(requests);
            send_error(conn->fd, request_id, ERR_EXECUTION_FAIL, "Failed to load requests");
            return;
        }
        int idx = find_request_index(requests, req_count, target_user, can_write_req);
        if (idx < 0) {
            free(requests);
            send_error(conn->fd, request_id, ERR_NO_REQUEST, "No matching request found");
            return;
        }
        remove_request_at(requests, &req_count, (size_t)idx);
        if (save_access_requests(state, filename, requests, req_count) != 0) {
            free(requests);
            send_error(conn->fd, request_id, ERR_EXECUTION_FAIL, "Failed to persist updated requests");
            return;
        }
        free(requests);
        char response[256];
        snprintf(response, sizeof(response),
                 "{\"status\":\"denied\",\"user\":\"%s\",\"mode\":\"%s\"}",
                 target_user,
                 can_write_req ? "write" : "read");
        message_header_t header = {
            .version = PROTOCOL_VERSION,
            .opcode = OP_COMMAND_STATUS,
            .request_id = request_id,
            .payload_len = (uint32_t)strlen(response)
        };
        uint8_t header_buf[PROTOCOL_HEADER_SIZE];
        if (protocol_encode_header(&header, header_buf) == 0) {
            send_all(conn->fd, header_buf, sizeof(header_buf));
            send_all(conn->fd, response, header.payload_len);
        }
        nm_log(state, "Denied %s access request to '%s' for '%s'",
               can_write_req ? "write" : "read", filename, target_user);
        
    } else if (strcmp(command, "ACLINFO") == 0) {
        if (filename[0] == '\0') {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing filename");
            return;
        }
        file_record_t *record = file_index_lookup(state->file_index, filename);
        if (record == NULL) {
            send_error(conn->fd, request_id, ERR_FILE_NOT_FOUND, "File not found");
            return;
        }
        int has_access = 0;
        if (strcmp(record->owner, username) == 0) {
            has_access = 1;
        } else {
            acl_entry_t *entry = file_record_find_acl(record, username);
            if (entry != NULL && (entry->can_read || entry->can_write)) {
                has_access = 1;
            }
        }
        if (!has_access) {
            send_error(conn->fd, request_id, ERR_NO_ACCESS, "Access denied");
            return;
        }
        char response[2048];
        int offset = 0;
        offset += snprintf(response + offset, sizeof(response) - offset,
                           "%s (RW)\n", record->owner);
        for (size_t i = 0; i < record->acl_count && offset < (int)sizeof(response) - 1; ++i) {
            const acl_entry_t *entry = &record->acl[i];
            char perms[3] = {0};
            size_t perm_idx = 0;
            if (entry->can_read) {
                perms[perm_idx++] = 'R';
            }
            if (entry->can_write) {
                perms[perm_idx++] = 'W';
            }
            if (perm_idx == 0) {
                perms[perm_idx++] = '-';
            }
            perms[perm_idx] = '\0';
            offset += snprintf(response + offset, sizeof(response) - offset,
                               "%s (%s)\n", entry->username, perms);
        }
        if (offset == 0) {
            snprintf(response, sizeof(response), "(no ACL entries)\n");
        }
        message_header_t header = {
            .version = PROTOCOL_VERSION,
            .opcode = OP_COMMAND_STATUS,
            .request_id = request_id,
            .payload_len = (uint32_t)strlen(response)
        };
        uint8_t header_buf[PROTOCOL_HEADER_SIZE];
        if (protocol_encode_header(&header, header_buf) == 0) {
            send_all(conn->fd, header_buf, sizeof(header_buf));
            send_all(conn->fd, response, header.payload_len);
        }
        nm_log(state, "Sent ACL info for '%s' to '%s'", filename, username);

    } else if (strcmp(command, "CHECKPOINT") == 0) {
        if (filename[0] == '\0') {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing filename");
            return;
        }
        char tag[128] = {0};
        if (!json_get_string(payload, "tag", tag, sizeof(tag)) || tag[0] == '\0') {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing checkpoint tag");
            return;
        }
        file_record_t *record = file_index_lookup(state->file_index, filename);
        if (record == NULL) {
            send_error(conn->fd, request_id, ERR_FILE_NOT_FOUND, "File not found");
            return;
        }
        if (!file_record_check_access(record, username, 1)) {
            send_error(conn->fd, request_id, ERR_NO_ACCESS, "Write access required");
            return;
        }
        storage_server_info_t *ss = select_storage_server_for_record(state, record);
        if (ss == NULL) {
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "No storage server available");
            return;
        }
    char ss_payload[1024];
        snprintf(ss_payload, sizeof(ss_payload),
                 "{\"filename\":\"%s\",\"tag\":\"%s\",\"username\":\"%s\"}",
                 filename, tag, username);
        message_header_t resp_header;
        char *resp_payload = NULL;
        if (forward_storage_request(ss, OP_CHECKPOINT_REQUEST, request_id, ss_payload,
                                    &resp_header, &resp_payload) != 0) {
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "Failed to contact storage server");
            return;
        }
        resp_header.request_id = request_id;
        uint8_t header_buf[PROTOCOL_HEADER_SIZE];
        protocol_encode_header(&resp_header, header_buf);
        send_all(conn->fd, header_buf, sizeof(header_buf));
        if (resp_payload != NULL && resp_header.payload_len > 0) {
            send_all(conn->fd, resp_payload, resp_header.payload_len);
        }
        free(resp_payload);
        nm_log(state, "Checkpoint '%s' requested for '%s' by '%s'", tag, filename, username);

    } else if (strcmp(command, "LISTCHECKPOINTS") == 0) {
        if (filename[0] == '\0') {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing filename");
            return;
        }
        file_record_t *record = file_index_lookup(state->file_index, filename);
        if (record == NULL) {
            send_error(conn->fd, request_id, ERR_FILE_NOT_FOUND, "File not found");
            return;
        }
        if (!file_record_check_access(record, username, 0)) {
            send_error(conn->fd, request_id, ERR_NO_ACCESS, "Access denied");
            return;
        }
        storage_server_info_t *ss = select_storage_server_for_record(state, record);
        if (ss == NULL) {
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "No storage server available");
            return;
        }
    char ss_payload[512];
        snprintf(ss_payload, sizeof(ss_payload),
                 "{\"filename\":\"%s\"}", filename);
        message_header_t resp_header;
        char *resp_payload = NULL;
        if (forward_storage_request(ss, OP_LISTCHECKPOINTS_REQUEST, request_id, ss_payload,
                                    &resp_header, &resp_payload) != 0) {
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "Failed to contact storage server");
            return;
        }
        resp_header.request_id = request_id;
        uint8_t header_buf[PROTOCOL_HEADER_SIZE];
        protocol_encode_header(&resp_header, header_buf);
        send_all(conn->fd, header_buf, sizeof(header_buf));
        if (resp_payload != NULL && resp_header.payload_len > 0) {
            send_all(conn->fd, resp_payload, resp_header.payload_len);
        }
        free(resp_payload);
        nm_log(state, "Listed checkpoints for '%s' to '%s'", filename, username);

    } else if (strcmp(command, "VIEWCHECKPOINT") == 0) {
        if (filename[0] == '\0') {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing filename");
            return;
        }
        char tag[128] = {0};
        if (!json_get_string(payload, "tag", tag, sizeof(tag)) || tag[0] == '\0') {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing checkpoint tag");
            return;
        }
        file_record_t *record = file_index_lookup(state->file_index, filename);
        if (record == NULL) {
            send_error(conn->fd, request_id, ERR_FILE_NOT_FOUND, "File not found");
            return;
        }
        if (!file_record_check_access(record, username, 0)) {
            send_error(conn->fd, request_id, ERR_NO_ACCESS, "Access denied");
            return;
        }
        storage_server_info_t *ss = select_storage_server_for_record(state, record);
        if (ss == NULL) {
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "No storage server available");
            return;
        }
    char ss_payload[1024];
        snprintf(ss_payload, sizeof(ss_payload),
                 "{\"filename\":\"%s\",\"tag\":\"%s\"}",
                 filename, tag);
        message_header_t resp_header;
        char *resp_payload = NULL;
        if (forward_storage_request(ss, OP_VIEWCHECKPOINT_REQUEST, request_id, ss_payload,
                                    &resp_header, &resp_payload) != 0) {
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "Failed to contact storage server");
            return;
        }
        resp_header.request_id = request_id;
        uint8_t header_buf[PROTOCOL_HEADER_SIZE];
        protocol_encode_header(&resp_header, header_buf);
        send_all(conn->fd, header_buf, sizeof(header_buf));
        if (resp_payload != NULL && resp_header.payload_len > 0) {
            send_all(conn->fd, resp_payload, resp_header.payload_len);
        }
        free(resp_payload);
        nm_log(state, "View checkpoint '%s' for '%s' by '%s'", tag, filename, username);

    } else if (strcmp(command, "REVERTCHECKPOINT") == 0) {
        if (filename[0] == '\0') {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing filename");
            return;
        }
        char tag[128] = {0};
        if (!json_get_string(payload, "tag", tag, sizeof(tag)) || tag[0] == '\0') {
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing checkpoint tag");
            return;
        }
        file_record_t *record = file_index_lookup(state->file_index, filename);
        if (record == NULL) {
            send_error(conn->fd, request_id, ERR_FILE_NOT_FOUND, "File not found");
            return;
        }
        if (!file_record_check_access(record, username, 1)) {
            send_error(conn->fd, request_id, ERR_NO_ACCESS, "Write access required");
            return;
        }
        storage_server_info_t *ss = select_storage_server_for_record(state, record);
        if (ss == NULL) {
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "No storage server available");
            return;
        }
    char ss_payload[1024];
        snprintf(ss_payload, sizeof(ss_payload),
                 "{\"filename\":\"%s\",\"tag\":\"%s\",\"username\":\"%s\"}",
                 filename, tag, username);
        message_header_t resp_header;
        char *resp_payload = NULL;
        if (forward_storage_request(ss, OP_REVERT_CHECKPOINT_REQUEST, request_id, ss_payload,
                                    &resp_header, &resp_payload) != 0) {
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "Failed to contact storage server");
            return;
        }
        resp_header.request_id = request_id;
        uint8_t header_buf[PROTOCOL_HEADER_SIZE];
        protocol_encode_header(&resp_header, header_buf);
        send_all(conn->fd, header_buf, sizeof(header_buf));
        if (resp_payload != NULL && resp_header.payload_len > 0) {
            send_all(conn->fd, resp_payload, resp_header.payload_len);
        }
        free(resp_payload);
        nm_log(state, "Reverted checkpoint '%s' for '%s' by '%s'", tag, filename, username);
        
    } else if (strcmp(command, "VIEW") == 0) {
        /* List files accessible to user */
        char response[8192] = {0};
        int offset = 0;
        
        /* Parse flags - check for true or 1 values in JSON */
        int show_all = (strstr(payload, "\"show_all\":true") != NULL || strstr(payload, "\"show_all\":1") != NULL) ? 1 : 0;
        int show_details = (strstr(payload, "\"show_details\":true") != NULL || strstr(payload, "\"show_details\":1") != NULL) ? 1 : 0;
        
        nm_log(state, "VIEW command: payload=%s, show_all=%d, show_details=%d", payload, show_all, show_details);
        
        /* Get all files from the in-memory file index */
        size_t file_count = 0;
        file_record_t **all_files = file_index_get_all(state->file_index, &file_count);
        
        if (all_files == NULL || file_count == 0) {
            offset = snprintf(response, sizeof(response), "(no files registered)");
        } else {
            /* Iterate through all files in the index */
            for (size_t i = 0; i < file_count; i++) {
                file_record_t *record = all_files[i];
                               if (record == NULL) {
                    continue;
                }
                
                const char *filename = record->filename;
                const char *owner = record->owner;
                
                /* Check if user has access using the in-memory ACL */
                int has_access = file_record_check_access(record, username, 0);
                
                /* Skip if no access and not showing all */
                if (!has_access && !show_all) {
                    continue;
                }
                
                /* Format output */
                if (show_details) {
                    /* Fetch live metadata from Storage Server */
                    unsigned long word_count = 0, char_count = 0;
                    char time_str[32] = "Never";
                    
                    nm_log(state, "VIEW -l: Processing file %s", filename);
                    
                    /* Use the record we already have from the loop */
                    if (record->ss_count > 0) {
                        nm_log(state, "VIEW -l: Found record for %s, ss_count=%zu", filename, record->ss_count);
                        /* Find the storage server */
                        storage_server_info_t *ss = find_storage_server(state, record->ss_ids[0]);
                        if (ss != NULL) {
                            nm_log(state, "VIEW -l: Found SS %s, connecting to port %u", 
                                   record->ss_ids[0], ss->client_port);
                            /* Connect to Storage Server */
                            int ss_fd = tcp_connect("localhost", ss->client_port);
                            if (ss_fd >= 0) {
                                nm_log(state, "VIEW -l: Connected to SS, sending INFO request");
                                /* Build INFO request */
                                char info_payload[512];
                                snprintf(info_payload, sizeof(info_payload), 
                                        "{\"filename\":\"%s\"}", filename);
                                
                                /* Send INFO request using low-level protocol */
                                message_header_t req_header = {
                                    .version = PROTOCOL_VERSION,
                                    .opcode = OP_INFO_REQUEST,
                                    .request_id = 0,
                                    .payload_len = (uint32_t)strlen(info_payload)
                                };
                                
                                uint8_t header_buf[PROTOCOL_HEADER_SIZE];
                                if (protocol_encode_header(&req_header, header_buf) == 0) {
                                    int send_rc1 = send_all(ss_fd, header_buf, sizeof(header_buf));
                                    int send_rc2 = send_all(ss_fd, info_payload, req_header.payload_len);
                                    nm_log(state, "VIEW -l: send_all results: header=%d, payload=%d", send_rc1, send_rc2);
                                    
                                    /* Receive response */
                                    uint8_t resp_header_buf[PROTOCOL_HEADER_SIZE];
                                    int recv_rc = recv_all(ss_fd, resp_header_buf, sizeof(resp_header_buf));
                                    nm_log(state, "VIEW -l: recv_all header result: %d", recv_rc);
                                    if (recv_rc == (int)sizeof(resp_header_buf)) {
                                        message_header_t resp_header;
                                        int decode_rc = protocol_decode_header(resp_header_buf, &resp_header);
                                        nm_log(state, "VIEW -l: decode_rc=%d for %s", decode_rc, filename);
                                        if (decode_rc == 0) {
                                            nm_log(state, "VIEW -l: Got opcode %u, payload_len %u for %s", 
                                                   resp_header.opcode, resp_header.payload_len, filename);
                                            
                                            if (resp_header.opcode != OP_ERROR && resp_header.payload_len > 0) {
                                                char *resp_payload = malloc(resp_header.payload_len + 1);
                                                if (resp_payload != NULL) {
                                                    int payload_rc = recv_all(ss_fd, resp_payload, resp_header.payload_len);
                                                    if (payload_rc == (int)resp_header.payload_len) {
                                                        resp_payload[resp_header.payload_len] = '\0';
                                                        nm_log(state, "VIEW -l: Response payload: %s", resp_payload);
                                                        json_get_uint(resp_payload, "word_count", &word_count);
                                                        json_get_uint(resp_payload, "char_count", &char_count);
                                                        
                                                        /* Get last_accessed timestamp from storage server */
                                                        unsigned long last_accessed = 0;
                                                        if (json_get_uint(resp_payload, "last_accessed", &last_accessed) && last_accessed > 0) {
                                                            time_t access_time = (time_t)last_accessed;
                                                            struct tm *tm_info = localtime(&access_time);
                                                            if (tm_info) {
                                                                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);
                                                            }
                                                        }
                                                        
                                                        nm_log(state, "VIEW -l: Parsed word_count=%lu, char_count=%lu, last_accessed=%lu", 
                                                               word_count, char_count, last_accessed);
                                                    } else {
                                                        nm_log(state, "VIEW -l: Failed to read payload for %s, rc=%d", filename, payload_rc);
                                                    }
                                                    free(resp_payload);
                                                }
                                            }
                                        }
                                    } else {
                                        nm_log(state, "VIEW -l: Failed to read header for %s, rc=%d", filename, recv_rc);
                                    }
                                }
                                close(ss_fd);
                            }
                        }
                    }
                    
                    offset += snprintf(response + offset, sizeof(response) - offset,
                                      "  %-15s│ %5lu │ %5lu │ %15s │ %-8s\n",
                                      filename, word_count, char_count, time_str, owner);
                } else {
                    offset += snprintf(response + offset, sizeof(response) - offset,
                                      "%s\n", filename);
                }
            }
            
            /* Free the allocated array */
            free(all_files);
        }
        
        if (offset == 0) {
            snprintf(response, sizeof(response), "(no accessible files)");
        }
        
        message_header_t header = {
            .version = PROTOCOL_VERSION,
            .opcode = OP_COMMAND_STATUS,
            .request_id = request_id,
            .payload_len = (uint32_t)strlen(response)
        };
        
        uint8_t header_buf[PROTOCOL_HEADER_SIZE];
        if (protocol_encode_header(&header, header_buf) == 0) {
            send_all(conn->fd, header_buf, sizeof(header_buf));
            send_all(conn->fd, response, header.payload_len);
        }
        nm_log(state, "Sent file list to user '%s' (all=%d, details=%d)", 
               username, show_all, show_details);
        
    } else if (strcmp(command, "LIST") == 0) {
        /* List all registered users */
        char response[2048] = {0};
        int offset = 0;
        
        client_info_t *client = state->clients;
        while (client != NULL) {
            offset += snprintf(response + offset, sizeof(response) - offset,
                              "--> %s\n", client->username);
            client = client->next;
        }
        
        if (offset == 0) {
            snprintf(response, sizeof(response), "(no users registered)");
        }
        
        message_header_t header = {
            .version = PROTOCOL_VERSION,
            .opcode = OP_COMMAND_STATUS,
            .request_id = request_id,
            .payload_len = (uint32_t)strlen(response)
        };
        
        uint8_t header_buf[PROTOCOL_HEADER_SIZE];
        if (protocol_encode_header(&header, header_buf) == 0) {
            send_all(conn->fd, header_buf, sizeof(header_buf));
            send_all(conn->fd, response, header.payload_len);
        }
        nm_log(state, "Sent user list");
        
    } else if (strcmp(command, "EXEC") == 0) {
        /* Execute file contents as shell commands */
        /* First, get the file content from SS */
        file_record_t *record = file_index_lookup(state->file_index, filename);
        if (record == NULL) {
            send_error(conn->fd, request_id, ERR_FILE_NOT_FOUND, "File not found");
            return;
        }
        
        /* Check read access */
        if (!file_record_check_access(record, username, 0)) {
            send_error(conn->fd, request_id, ERR_NO_ACCESS, "No read access to file");
            return;
        }
        
        /* Select an SS to fetch content from */
        if (record->ss_count == 0) {
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "No storage server available");
            return;
        }
        
        storage_server_info_t *ss_info = find_storage_server(state, record->ss_ids[0]);
        if (ss_info == NULL) {
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "Storage server unavailable");
            return;
        }
        
        /* Connect to SS and fetch file content */
        int ss_fd = tcp_connect("localhost", ss_info->client_port);
        if (ss_fd < 0) {
            nm_log(state, "EXEC: Failed to connect to SS on port %u", ss_info->client_port);
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "Cannot connect to storage server");
            return;
        }
        
        nm_log(state, "EXEC: Connected to SS, fetching '%s'", filename);
        
        char read_req[512];
        snprintf(read_req, sizeof(read_req), "{\"filename\":\"%s\",\"username\":\"%s\"}", filename, username);
        
        message_header_t req_hdr = {
            .version = PROTOCOL_VERSION,
            .opcode = OP_DATA_REQUEST,
            .request_id = 0,
            .payload_len = (uint32_t)strlen(read_req)
        };
        
        uint8_t req_buf[PROTOCOL_HEADER_SIZE];
        int encode_rc = protocol_encode_header(&req_hdr, req_buf);
        int send_rc1 = send_all(ss_fd, req_buf, sizeof(req_buf));
        int send_rc2 = send_all(ss_fd, read_req, req_hdr.payload_len);
        
        nm_log(state, "EXEC: encode=%d, send_header=%d, send_payload=%d", encode_rc, send_rc1, send_rc2);
        
        if (encode_rc != 0 || send_rc1 != (int)sizeof(req_buf) || send_rc2 != (int)req_hdr.payload_len) {
            close(ss_fd);
            nm_log(state, "EXEC: Failed to send request to SS (expected header=%zu, payload=%u)", 
                   sizeof(req_buf), req_hdr.payload_len);
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "Failed to fetch file");
            return;
        }
        
        /* Receive file content from SS */
        message_header_t resp_hdr;
        char *file_content = NULL;
        uint8_t resp_buf[PROTOCOL_HEADER_SIZE];
        
        int recv_rc = recv_all(ss_fd, resp_buf, sizeof(resp_buf));
        nm_log(state, "EXEC: recv_header=%d (expected %zu)", recv_rc, sizeof(resp_buf));
        
        if (recv_rc != (int)sizeof(resp_buf)) {
            close(ss_fd);
            nm_log(state, "EXEC: Failed to receive response header");
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "Failed to receive response");
            return;
        }
        
        int decode_rc = protocol_decode_header(resp_buf, &resp_hdr);
        nm_log(state, "EXEC: decode=%d, opcode=%u, payload_len=%u", decode_rc, resp_hdr.opcode, resp_hdr.payload_len);
        
        if (decode_rc != 0) {
            close(ss_fd);
            nm_log(state, "EXEC: Failed to decode response header");
            send_error(conn->fd, request_id, ERR_PROTOCOL, "Invalid response");
            return;
        }
        
        if (resp_hdr.payload_len > 0) {
            file_content = malloc(resp_hdr.payload_len + 1);
            if (file_content != NULL) {
                int payload_recv = recv_all(ss_fd, file_content, resp_hdr.payload_len);
                nm_log(state, "EXEC: recv_payload=%d (expected %u)", payload_recv, resp_hdr.payload_len);
                if (payload_recv == (int)resp_hdr.payload_len) {
                    file_content[resp_hdr.payload_len] = '\0';
                } else {
                    nm_log(state, "EXEC: Failed to receive full payload");
                    free(file_content);
                    file_content = NULL;
                }
            }
        }
        close(ss_fd);
        
        if (file_content == NULL || resp_hdr.opcode == OP_ERROR) {
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "Failed to read file");
            free(file_content);
            return;
        }
        
        /* Parse JSON to extract actual content */
        char content[16384] = {0};
        char *content_start = strstr(file_content, "\"content\":\"");
        if (content_start != NULL) {
            content_start += 11; /* Skip past "content":" */
            char *content_end = strstr(content_start, "\"}");
            if (content_end != NULL) {
                size_t len = content_end - content_start;
                if (len < sizeof(content)) {
                    memcpy(content, content_start, len);
                    content[len] = '\0';
                }
            }
        }
        free(file_content);
        
        if (content[0] == '\0') {
            send_error(conn->fd, request_id, ERR_EXECUTION_FAIL, "Empty file");
            return;
        }
        
        /* Execute commands using popen */
        FILE *fp = popen(content, "r");
        if (fp == NULL) {
            send_error(conn->fd, request_id, ERR_EXECUTION_FAIL, "Failed to execute commands");
            return;
        }
        
        /* Capture output */
        char output[8192] = {0};
        size_t offset = 0;
        char line[1024];
        while (fgets(line, sizeof(line), fp) != NULL && offset < sizeof(output) - 1) {
            size_t line_len = strlen(line);
            if (offset + line_len < sizeof(output)) {
                memcpy(output + offset, line, line_len);
                offset += line_len;
            }
        }
        output[offset] = '\0';
        
        int status = pclose(fp);
        
        /* Send output back to client */
        char response[10240];
        if (status == 0) {
            snprintf(response, sizeof(response),
                    "=== Execution Output ===\n%s\n=== Exit Code: 0 ===",
                    output[0] != '\0' ? output : "(no output)");
        } else {
            snprintf(response, sizeof(response),
                    "=== Execution Output ===\n%s\n=== Exit Code: %d ===",
                    output[0] != '\0' ? output : "(no output)", WEXITSTATUS(status));
        }
        
        message_header_t header = {
            .version = PROTOCOL_VERSION,
            .opcode = OP_COMMAND_STATUS,
            .request_id = request_id,
            .payload_len = (uint32_t)strlen(response)
        };
        
        uint8_t header_buf[PROTOCOL_HEADER_SIZE];
        if (protocol_encode_header(&header, header_buf) == 0) {
            send_all(conn->fd, header_buf, sizeof(header_buf));
            send_all(conn->fd, response, header.payload_len);
        }
        nm_log(state, "EXEC command for '%s' by '%s' (exit code: %d)", filename, username, status);
        
    } else {
        send_error(conn->fd, request_id, ERR_PROTOCOL, "Unknown command");
    }
}
static void handle_createfolder(nm_state_t *state, connection_t *conn, uint32_t request_id, const char *payload) {
    char foldername[MAX_FILENAME_LEN] = {0};
    char username[128] = {0};

    if (!json_get_string(payload, "foldername", foldername, sizeof(foldername))) {
        send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing foldername");
        return;
    }

    if (!json_get_string(payload, "username", username, sizeof(username))) {
        send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing username");
        return;
    }

    if (state->servers == NULL) {
        send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "No storage servers available");
        return;
    }

    uint8_t header_buf[PROTOCOL_HEADER_SIZE];
    size_t payload_len = strlen(payload);
    int total_servers = 0;
    int success_count = 0;
    int exists_count = 0;
    char error_message[256] = {0};

    for (storage_server_info_t *cur = state->servers; cur != NULL; cur = cur->next) {
        total_servers++;
        int ss_fd = tcp_connect("localhost", cur->client_port);
        if (ss_fd < 0) {
            if (error_message[0] == '\0') {
                snprintf(error_message, sizeof(error_message), "Failed to connect to storage server %s", cur->ss_id);
            }
            continue;
        }

        message_header_t req_header = {
            .version = PROTOCOL_VERSION,
            .opcode = OP_CREATEFOLDER_REQUEST,
            .request_id = request_id,
            .payload_len = (uint32_t)payload_len
        };

        protocol_encode_header(&req_header, header_buf);
        if (send_all(ss_fd, header_buf, sizeof(header_buf)) != sizeof(header_buf) ||
            send_all(ss_fd, payload, payload_len) != (ssize_t)payload_len) {
            if (error_message[0] == '\0') {
                snprintf(error_message, sizeof(error_message), "Failed to send create request to %s", cur->ss_id);
            }
            close(ss_fd);
            continue;
        }

        if (recv_all(ss_fd, header_buf, sizeof(header_buf)) != sizeof(header_buf)) {
            if (error_message[0] == '\0') {
                snprintf(error_message, sizeof(error_message), "Failed to receive response from %s", cur->ss_id);
            }
            close(ss_fd);
            continue;
        }

        message_header_t resp_header;
        protocol_decode_header(header_buf, &resp_header);

        char *resp_payload = NULL;
        if (resp_header.payload_len > 0) {
            resp_payload = calloc(1, resp_header.payload_len + 1);
            if (resp_payload != NULL) {
                if (recv_all(ss_fd, resp_payload, resp_header.payload_len) != (ssize_t)resp_header.payload_len) {
                    free(resp_payload);
                    resp_payload = NULL;
                }
            }
        }
        close(ss_fd);

        if (resp_header.opcode == OP_COMMAND_STATUS) {
            success_count++;
        } else if (resp_header.opcode == OP_ERROR && resp_payload != NULL &&
                   strstr(resp_payload, "Folder already exists") != NULL) {
            exists_count++;
        } else {
            if (error_message[0] == '\0') {
                if (resp_payload != NULL) {
                    char parsed[256] = {0};
                    if (!json_get_string(resp_payload, "error", parsed, sizeof(parsed))) {
                        json_get_string(resp_payload, "message", parsed, sizeof(parsed));
                    }
                    if (parsed[0] != '\0') {
                        strncpy(error_message, parsed, sizeof(error_message) - 1);
                        error_message[sizeof(error_message) - 1] = '\0';
                    } else {
                        strncpy(error_message, resp_payload, sizeof(error_message) - 1);
                        error_message[sizeof(error_message) - 1] = '\0';
                    }
                } else {
                    snprintf(error_message, sizeof(error_message), "Storage server %s returned an error", cur->ss_id);
                }
            }
        }

        free(resp_payload);
    }

    if (success_count > 0) {
        char response[256];
        snprintf(response, sizeof(response), "{\"status\":\"created\",\"replicated\":%d}", success_count);
        message_header_t resp_header = {
            .version = PROTOCOL_VERSION,
            .opcode = OP_COMMAND_STATUS,
            .request_id = request_id,
            .payload_len = (uint32_t)strlen(response)
        };
        protocol_encode_header(&resp_header, header_buf);
        send_all(conn->fd, header_buf, sizeof(header_buf));
        send_all(conn->fd, response, resp_header.payload_len);
        nm_log(state, "User %s created folder '%s' on %d storage server(s)", username, foldername, success_count);
        return;
    }

    if (exists_count == total_servers && total_servers > 0) {
        send_error(conn->fd, request_id, ERR_PROTOCOL, "Folder already exists");
        nm_log(state, "User %s attempted to create existing folder '%s'", username, foldername);
        return;
    }

    if (error_message[0] == '\0') {
        snprintf(error_message, sizeof(error_message), "Unable to create folder '%.128s' on any storage server", foldername);
    }
    send_error(conn->fd, request_id, ERR_STORAGE_DOWN, error_message);
    nm_log(state, "User %s failed to create folder '%s': %s", username, foldername, error_message);
}

static void handle_move(nm_state_t *state, connection_t *conn, uint32_t request_id, const char *payload) {
    char filename[MAX_FILENAME_LEN] = {0};
    char foldername[MAX_FILENAME_LEN] = {0};
    char username[MAX_USERNAME_LEN] = {0};

    if (!json_get_string(payload, "filename", filename, sizeof(filename))) {
        send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing filename");
        return;
    }
    json_get_string(payload, "foldername", foldername, sizeof(foldername));
    json_get_string(payload, "username", username, sizeof(username));

    file_record_t *record = file_index_lookup(state->file_index, filename);
    storage_server_info_t *ss = NULL;

    if (record != NULL && record->ss_count > 0) {
        ss = find_storage_server(state, record->ss_ids[0]);
    }
    if (ss == NULL) {
        ss = state->servers;
    }

    if (ss == NULL) {
        send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "Storage server not available");
        return;
    }

    bool needs_folder = foldername[0] != '\0' && strcmp(foldername, "/") != 0 && username[0] != '\0';
    if (needs_folder) {
        char ensure_payload[1024];
        snprintf(ensure_payload, sizeof(ensure_payload),
                 "{\"foldername\":\"%s\",\"username\":\"%s\"}",
                 foldername, username);
        message_header_t ensure_header;
        char *ensure_resp_payload = NULL;
        if (forward_storage_request(ss, OP_CREATEFOLDER_REQUEST, 0, ensure_payload,
                                    &ensure_header, &ensure_resp_payload) == 0) {
            bool already_exists = ensure_resp_payload &&
                                  strstr(ensure_resp_payload, "already exists") != NULL;
            if (ensure_header.opcode == OP_ERROR && !already_exists) {
                char err_msg[256] = {0};
                if (!(json_get_string(ensure_resp_payload, "error", err_msg, sizeof(err_msg)) ||
                      json_get_string(ensure_resp_payload, "message", err_msg, sizeof(err_msg)))) {
                    if (ensure_resp_payload != NULL) {
                        strncpy(err_msg, ensure_resp_payload, sizeof(err_msg) - 1);
                        err_msg[sizeof(err_msg) - 1] = '\0';
                    }
                }
                send_error(conn->fd, request_id, ERR_EXECUTION_FAIL,
                           err_msg[0] ? err_msg : "Unable to prepare destination folder");
                free(ensure_resp_payload);
                return;
            }
        }
        free(ensure_resp_payload);
    }

    int ss_fd = tcp_connect("localhost", ss->client_port);
    if (ss_fd < 0) {
        send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "Failed to connect to storage server");
        return;
    }

    uint8_t header_buf[PROTOCOL_HEADER_SIZE];
    size_t payload_len = strlen(payload);
    message_header_t req_header = {
        .version = PROTOCOL_VERSION,
        .opcode = OP_MOVE_REQUEST,
        .request_id = request_id,
        .payload_len = (uint32_t)payload_len
    };

    if (protocol_encode_header(&req_header, header_buf) != 0 ||
        send_all(ss_fd, header_buf, sizeof(header_buf)) != (ssize_t)sizeof(header_buf) ||
        send_all(ss_fd, payload, payload_len) != (ssize_t)payload_len) {
        close(ss_fd);
        send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "Failed to send move request to storage server");
        return;
    }

    if (recv_all(ss_fd, header_buf, sizeof(header_buf)) != (ssize_t)sizeof(header_buf)) {
        close(ss_fd);
        send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "Failed to receive response from storage server");
        return;
    }

    message_header_t resp_header;
    if (protocol_decode_header(header_buf, &resp_header) != 0) {
        close(ss_fd);
        send_error(conn->fd, request_id, ERR_PROTOCOL, "Invalid response from storage server");
        return;
    }

    char *resp_payload = NULL;
    if (resp_header.payload_len > 0) {
        resp_payload = calloc(1, resp_header.payload_len + 1);
        if (resp_payload == NULL ||
            recv_all(ss_fd, resp_payload, resp_header.payload_len) != (ssize_t)resp_header.payload_len) {
            free(resp_payload);
            close(ss_fd);
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "Failed to receive move response payload");
            return;
        }
        resp_payload[resp_header.payload_len] = '\0';
    }
    close(ss_fd);

    if (resp_header.opcode == OP_COMMAND_STATUS && record != NULL) {
        char foldername[MAX_FILENAME_LEN] = {0};
        json_get_string(payload, "foldername", foldername, sizeof(foldername));

        char new_path[512];
        if (foldername[0] == '\0' || strcmp(foldername, "/") == 0) {
            snprintf(new_path, sizeof(new_path), "%s", filename);
        } else if (foldername[strlen(foldername) - 1] == '/') {
            snprintf(new_path, sizeof(new_path), "%s%s", foldername, filename);
        } else {
            snprintf(new_path, sizeof(new_path), "%s/%s", foldername, filename);
        }

        acl_entry_t *saved_acl = NULL;
        size_t saved_acl_count = 0;
        time_t created_at = record->created_at;
        time_t modified_at = record->modified_at;
        size_t word_count = record->word_count;
        size_t char_count = record->char_count;
        size_t sentence_count = record->sentence_count;
        char owner_copy[MAX_USERNAME_LEN];
        strncpy(owner_copy, record->owner, sizeof(owner_copy) - 1);
        owner_copy[sizeof(owner_copy) - 1] = '\0';
        char ss_ids_copy[MAX_SS_PER_FILE][128] = {{0}};
        size_t ss_count_copy = record->ss_count;
        for (size_t i = 0; i < ss_count_copy && i < MAX_SS_PER_FILE; i++) {
            strncpy(ss_ids_copy[i], record->ss_ids[i], sizeof(ss_ids_copy[i]) - 1);
            ss_ids_copy[i][sizeof(ss_ids_copy[i]) - 1] = '\0';
        }

        if (record->acl_count > 0 && record->acl != NULL) {
            saved_acl = calloc(record->acl_count, sizeof(acl_entry_t));
            if (saved_acl != NULL) {
                memcpy(saved_acl, record->acl, record->acl_count * sizeof(acl_entry_t));
                saved_acl_count = record->acl_count;
            }
        }

        char old_acl_path[PATH_MAX], new_acl_path[PATH_MAX];
        if (ns_acl_path(old_acl_path, sizeof(old_acl_path), state->data_dir, filename) == 0 &&
            ns_acl_path(new_acl_path, sizeof(new_acl_path), state->data_dir, new_path) == 0) {
            char *last_slash = strrchr(new_acl_path, '/');
            if (last_slash != NULL) {
                *last_slash = '\0';
                char temp_path[PATH_MAX];
                strncpy(temp_path, new_acl_path, sizeof(temp_path) - 1);
                temp_path[sizeof(temp_path) - 1] = '\0';
                for (char *p = temp_path + 1; *p; ++p) {
                    if (*p == '/') {
                        *p = '\0';
                        mkdir(temp_path, 0755);
                        *p = '/';
                    }
                }
                mkdir(temp_path, 0755);
                *last_slash = '/';
            }
            rename(old_acl_path, new_acl_path);
        }

        char old_req_path[PATH_MAX], new_req_path[PATH_MAX];
        if (ns_requests_path(old_req_path, sizeof(old_req_path), state->data_dir, filename) == 0 &&
            ns_requests_path(new_req_path, sizeof(new_req_path), state->data_dir, new_path) == 0) {
            ensure_parent_dirs(new_req_path);
            rename(old_req_path, new_req_path);
        }

        file_index_remove(state->file_index, filename);
        if (ss_count_copy > 0) {
            file_index_add(state->file_index, new_path, owner_copy, ss_ids_copy[0]);
        } else {
            file_index_add(state->file_index, new_path, owner_copy, ss != NULL ? ss->ss_id : "");
        }

        for (size_t i = 1; i < ss_count_copy; i++) {
            file_index_add_ss_replica(state->file_index, new_path, ss_ids_copy[i]);
        }

        file_record_t *new_record = file_index_lookup(state->file_index, new_path);
        if (new_record != NULL) {
            new_record->created_at = created_at;
            new_record->modified_at = modified_at;
            new_record->word_count = word_count;
            new_record->char_count = char_count;
            new_record->sentence_count = sentence_count;

            if (saved_acl != NULL && saved_acl_count > 0) {
                new_record->acl = saved_acl;
                new_record->acl_count = saved_acl_count;
                new_record->acl_capacity = saved_acl_count;
                saved_acl = NULL;
            }
        }

        if (saved_acl != NULL) {
            free(saved_acl);
        }

        lru_cache_invalidate(state->cache, filename);
        lru_cache_invalidate(state->cache, new_path);
    }

    protocol_encode_header(&resp_header, header_buf);
    send_all(conn->fd, header_buf, sizeof(header_buf));
    if (resp_header.payload_len > 0 && resp_payload != NULL) {
        send_all(conn->fd, resp_payload, resp_header.payload_len);
    }
    free(resp_payload);

    nm_log(state, "File '%s' move request forwarded", filename);
}

static void handle_viewfolder(nm_state_t *state, connection_t *conn, uint32_t request_id, const char *payload) {
    char foldername[MAX_FILENAME_LEN] = {0};
    char username[128] = {0};

    if (!json_get_string(payload, "foldername", foldername, sizeof(foldername))) {
        send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing foldername");
        return;
    }
    if (!json_get_string(payload, "username", username, sizeof(username))) {
        send_error(conn->fd, request_id, ERR_PROTOCOL, "Missing username");
        return;
    }
    if (state->servers == NULL) {
        send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "No storage servers available");
        return;
    }

    string_list_t files = {0};
    string_list_t folders = {0};
    int success_servers = 0;
    int folder_not_found = 0;
    int contacted_servers = 0;
    char error_message[256] = {0};
    uint8_t header_buf[PROTOCOL_HEADER_SIZE];
    size_t payload_len = strlen(payload);

    for (storage_server_info_t *cur = state->servers; cur != NULL; cur = cur->next) {
        int ss_fd = tcp_connect("localhost", cur->client_port);
        if (ss_fd < 0) {
            if (error_message[0] == '\0') {
                snprintf(error_message, sizeof(error_message), "Failed to connect to storage server %s", cur->ss_id);
            }
            continue;
        }

        contacted_servers++;

        message_header_t req_header = {
            .version = PROTOCOL_VERSION,
            .opcode = OP_VIEWFOLDER_REQUEST,
            .request_id = request_id,
            .payload_len = (uint32_t)payload_len
        };

        if (protocol_encode_header(&req_header, header_buf) != 0 ||
            send_all(ss_fd, header_buf, sizeof(header_buf)) != (ssize_t)sizeof(header_buf) ||
            send_all(ss_fd, payload, payload_len) != (ssize_t)payload_len) {
            if (error_message[0] == '\0') {
                snprintf(error_message, sizeof(error_message), "Failed to send view request to %s", cur->ss_id);
            }
            close(ss_fd);
            continue;
        }

        if (recv_all(ss_fd, header_buf, sizeof(header_buf)) != (ssize_t)sizeof(header_buf)) {
            if (error_message[0] == '\0') {
                snprintf(error_message, sizeof(error_message), "Failed to receive response from %s", cur->ss_id);
            }
            close(ss_fd);
            continue;
        }

        message_header_t resp_header;
        if (protocol_decode_header(header_buf, &resp_header) != 0) {
            if (error_message[0] == '\0') {
                snprintf(error_message, sizeof(error_message), "Invalid response from %s", cur->ss_id);
            }
            close(ss_fd);
            continue;
        }

        char *resp_payload = NULL;
        if (resp_header.payload_len > 0) {
            resp_payload = calloc(1, resp_header.payload_len + 1);
            if (resp_payload == NULL ||
                recv_all(ss_fd, resp_payload, resp_header.payload_len) != (ssize_t)resp_header.payload_len) {
                if (error_message[0] == '\0') {
                    snprintf(error_message, sizeof(error_message), "Failed to read response payload from %s", cur->ss_id);
                }
                free(resp_payload);
                resp_payload = NULL;
                close(ss_fd);
                continue;
            }
            resp_payload[resp_header.payload_len] = '\0';
        }
        close(ss_fd);

        if (resp_header.opcode == OP_VIEWFOLDER_RESPONSE && resp_payload != NULL) {
            success_servers++;
            parse_json_string_array(resp_payload, "files", &files);
            parse_json_string_array(resp_payload, "folders", &folders);
        } else if (resp_header.opcode == OP_ERROR) {
            char parsed[256] = {0};
            if (resp_payload != NULL) {
                if (!json_get_string(resp_payload, "error", parsed, sizeof(parsed))) {
                    json_get_string(resp_payload, "message", parsed, sizeof(parsed));
                }
            }
            const char *err_text = parsed[0] != '\0' ? parsed : (resp_payload != NULL ? resp_payload : "Unknown error");
            if (strstr(err_text, "not found") != NULL) {
                folder_not_found++;
            }
            if (error_message[0] == '\0') {
                strncpy(error_message, err_text, sizeof(error_message) - 1);
                error_message[sizeof(error_message) - 1] = '\0';
            }
        } else {
            if (error_message[0] == '\0') {
                snprintf(error_message, sizeof(error_message), "Unexpected response from %s", cur->ss_id);
            }
        }

        free(resp_payload);
    }

    if (success_servers == 0) {
        if (folder_not_found == contacted_servers && contacted_servers > 0) {
            send_error(conn->fd, request_id, ERR_FILE_NOT_FOUND, "Folder not found");
        } else if (error_message[0] != '\0') {
            send_error(conn->fd, request_id, ERR_EXECUTION_FAIL, error_message);
        } else {
            send_error(conn->fd, request_id, ERR_STORAGE_DOWN, "Unable to list folder on any storage server");
        }
        string_list_free(&files);
        string_list_free(&folders);
        return;
    }

    char response[16384];
    size_t offset = snprintf(response, sizeof(response), "{\"files\":[");
    for (size_t i = 0; i < files.count && offset < sizeof(response) - 2; ++i) {
        if (i > 0) {
            response[offset++] = ',';
        }
        append_json_string_literal(response, sizeof(response), &offset, files.items[i]);
    }
    offset += snprintf(response + offset, sizeof(response) - offset, "],\"folders\":[");
    for (size_t i = 0; i < folders.count && offset < sizeof(response) - 2; ++i) {
        if (i > 0) {
            response[offset++] = ',';
        }
        append_json_string_literal(response, sizeof(response), &offset, folders.items[i]);
    }
    snprintf(response + offset, sizeof(response) - offset, "]}");

    message_header_t resp_header = {
        .version = PROTOCOL_VERSION,
        .opcode = OP_VIEWFOLDER_RESPONSE,
        .request_id = request_id,
        .payload_len = (uint32_t)strlen(response)
    };

    protocol_encode_header(&resp_header, header_buf);
    send_all(conn->fd, header_buf, sizeof(header_buf));
    send_all(conn->fd, response, resp_header.payload_len);

    string_list_free(&files);
    string_list_free(&folders);
    nm_log(state, "Aggregated folder view from %d storage server(s)", success_servers);
}

static void handle_payload(nm_state_t *state, connection_t *conn, const message_header_t *header, const char *payload) {
    if (header->version != PROTOCOL_VERSION) {
        send_error(conn->fd, header->request_id, ERR_PROTOCOL, "Unsupported protocol version");
        return;
    }
    switch (header->opcode) {
        case OP_REGISTER_SS:
            handle_register_ss(state, conn, header->request_id, payload);
            break;
        case OP_REGISTER_CLIENT:
            handle_register_client(state, conn, header->request_id, payload);
            break;
        case OP_HEARTBEAT:
            handle_heartbeat(state, conn, header->request_id, payload);
            break;
        case OP_LOOKUP_FILE:
            handle_lookup_file(state, conn, header->request_id, payload);
            break;
        case OP_COMMAND_FORWARD:
            handle_command_forward(state, conn, header->request_id, payload);
            break;
        case OP_CREATEFOLDER_REQUEST:
            handle_createfolder(state, conn, header->request_id, payload);
            break;
        case OP_MOVE_REQUEST:
            handle_move(state, conn, header->request_id, payload);
            break;
        case OP_VIEWFOLDER_REQUEST:
            handle_viewfolder(state, conn, header->request_id, payload);
            break;
        default:
            send_error(conn->fd, header->request_id, ERR_PROTOCOL, "Unsupported opcode");
            break;
    }
}

static int handle_connection_data(nm_state_t *state, connection_t *conn) {
    uint8_t header_buf[PROTOCOL_HEADER_SIZE];
    int received = recv_all(conn->fd, header_buf, sizeof(header_buf));
    if (received == 0) {
        nm_log(state, "Connection closed by peer (%s)", conn->identifier);
        return -1;
    }
    if (received < 0) {
        nm_log(state, "recv error: %s", strerror(errno));
        return -1;
    }

    message_header_t header;
    if (protocol_decode_header(header_buf, &header) != 0) {
        nm_log(state, "Failed to decode header");
        return -1;
    }

    char *payload = NULL;
    if (header.payload_len > 0) {
        payload = calloc(1, header.payload_len + 1);
        if (payload == NULL) {
            nm_log(state, "Out of memory for payload");
            return -1;
        }
        int got = recv_all(conn->fd, payload, header.payload_len);
        if (got <= 0) {
            nm_log(state, "Failed to receive payload");
            free(payload);
            return -1;
        }
        payload[header.payload_len] = '\0';
    }

    handle_payload(state, conn, &header, payload != NULL ? payload : "");
    free(payload);
    return 0;
}

static int prepare_directories(nm_state_t *state) {
    const char *subdirs[] = {
        "acl",
        "cache",
        "logs",
        "requests"
    };
    if (ensure_dir(state->data_dir) != 0) {
        return -1;
    }
    char path[PATH_MAX];
    for (size_t i = 0; i < ARRAY_SIZE(subdirs); ++i) {
        int written = snprintf(path, sizeof(path), "%s/%s", state->data_dir, subdirs[i]);
        if (written < 0 || (size_t)written >= sizeof(path)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (ensure_dir(path) != 0) {
            return -1;
        }
    }
    return 0;
}

static void close_all_connections(nm_state_t *state) {
    connection_t *conn = state->connections;
    while (conn != NULL) {
        connection_t *next = conn->next;
        close(conn->fd);
        free(conn);
        conn = next;
    }
    state->connections = NULL;
}

static void free_registry(nm_state_t *state) {
    storage_server_info_t *ss = state->servers;
    while (ss != NULL) {
        storage_server_info_t *next = ss->next;
        free(ss);
        ss = next;
    }
    state->servers = NULL;

    client_info_t *cl = state->clients;
    while (cl != NULL) {
        client_info_t *next = cl->next;
        free(cl);
        cl = next;
    }
    state->clients = NULL;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <listen-port> <data-dir>\n", argv[0]);
        return EXIT_FAILURE;
    }

    uint16_t port = (uint16_t)strtoul(argv[1], NULL, 10);
    nm_state_t state;
    memset(&state, 0, sizeof(state));
    strncpy(state.data_dir, argv[2], sizeof(state.data_dir) - 1);

    if (prepare_directories(&state) != 0) {
        fprintf(stderr, "Failed to prepare data directories: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if (open_log_file(&state) != 0) {
        fprintf(stderr, "Failed to open log file: %s\n", strerror(errno));
    }

    if (file_index_init(&state.file_index) != 0) {
        fprintf(stderr, "Failed to initialize file index: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    
    /* Load existing files and users from registry */
    char registry_path[PATH_MAX];
    ns_registry_path(registry_path, sizeof(registry_path), state.data_dir);
    FILE *registry = fopen(registry_path, "r");
    if (registry != NULL) {
        char line[1024];
        int file_count = 0;
        int user_count = 0;
        while (fgets(line, sizeof(line), registry)) {
            if (strstr(line, "\"type\":\"FILE_CREATE\"") != NULL) {
                char filename[256] = {0};
                char owner[128] = {0};
                char ss_id[128] = {0};
                
                if (json_get_string(line, "filename", filename, sizeof(filename)) &&
                    json_get_string(line, "owner", owner, sizeof(owner)) &&
                    json_get_string(line, "ss_id", ss_id, sizeof(ss_id))) {
                    
                    file_index_add(state.file_index, filename, owner, ss_id);
                    
                    /* Load ACL from disk if it exists */
                    char acl_path[PATH_MAX];
                    if (ns_acl_path(acl_path, sizeof(acl_path), state.data_dir, filename) == 0) {
                        FILE *acl_file = fopen(acl_path, "r");
                        if (acl_file != NULL) {
                            file_record_t *record = file_index_lookup(state.file_index, filename);
                            if (record != NULL) {
                                char acl_line[256];
                                while (fgets(acl_line, sizeof(acl_line), acl_file)) {
                                    char user[128] = {0};
                                    char perm[8] = {0};
                                    if (sscanf(acl_line, "%127[^:]:%7s", user, perm) == 2) {
                                        /* Skip owner entry (already set) */
                                        if (strcmp(user, owner) == 0) {
                                            continue;
                                        }
                                        int can_read = (strchr(perm, 'R') != NULL);
                                        int can_write = (strchr(perm, 'W') != NULL);
                                        file_record_add_acl(record, user, can_read, can_write);
                                    }
                                }
                            }
                            fclose(acl_file);
                        }
                    }
                    
                    file_count++;
                }
            } else if (strstr(line, "\"type\":\"CLIENT_REGISTER\"") != NULL) {
                char username[128] = {0};
                if (json_get_string(line, "username", username, sizeof(username))) {
                    if (find_client(&state, username) == NULL) {
                        client_info_t *info = calloc(1, sizeof(*info));
                        if (info != NULL) {
                            strncpy(info->username, username, sizeof(info->username) - 1);
                            info->next = state.clients;
                            state.clients = info;
                            user_count++;
                        }
                    }
                }
            }
        }
        fclose(registry);
        if (file_count > 0) {
            nm_log(&state, "Loaded %d files from registry", file_count);
        }
        if (user_count > 0) {
            nm_log(&state, "Loaded %d registered users from registry", user_count);
        }
    }

    if (lru_cache_init(&state.cache, 128) != 0) {
        fprintf(stderr, "Failed to initialize cache: %s\n", strerror(errno));
        file_index_destroy(state.file_index);
        return EXIT_FAILURE;
    }

    state.listen_fd = tcp_listen(NULL, port, 64);
    if (state.listen_fd < 0) {
        fprintf(stderr, "Failed to listen on port %u: %s\n", port, strerror(errno));
        lru_cache_destroy(state.cache);
        file_index_destroy(state.file_index);
        return EXIT_FAILURE;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    nm_log(&state, "Name Server listening on port %u", port);
    nm_log(&state, "File index and LRU cache (capacity 128) initialized");

    while (keep_running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(state.listen_fd, &read_fds);
        int max_fd = state.listen_fd;

        for (connection_t *conn = state.connections; conn != NULL; conn = conn->next) {
            FD_SET(conn->fd, &read_fds);
            if (conn->fd > max_fd) {
                max_fd = conn->fd;
            }
        }

        struct timeval timeout = {
            .tv_sec = 1,
            .tv_usec = 0
        };

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            nm_log(&state, "select error: %s", strerror(errno));
            break;
        }

        if (ready == 0) {
            continue;
        }

        if (FD_ISSET(state.listen_fd, &read_fds)) {
            struct sockaddr_storage addr;
            socklen_t addrlen = sizeof(addr);
            int fd = accept(state.listen_fd, (struct sockaddr *)&addr, &addrlen);
            if (fd >= 0) {
                add_connection(&state, fd);
            }
        }

        connection_t *conn = state.connections;
        while (conn != NULL) {
            connection_t *next = conn->next;
            if (FD_ISSET(conn->fd, &read_fds)) {
                if (handle_connection_data(&state, conn) != 0) {
                    remove_connection(&state, conn);
                }
            }
            conn = next;
        }
    }

    nm_log(&state, "Shutting down Name Server");
    close_all_connections(&state);
    free_registry(&state);
    lru_cache_destroy(state.cache);
    file_index_destroy(state.file_index);
    close(state.listen_fd);
    if (state.log_file != NULL) {
        fclose(state.log_file);
    }
    return EXIT_SUCCESS;
}

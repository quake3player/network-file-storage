#define _POSIX_C_SOURCE 200809L

#include "persistence.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int write_path(char *buf, size_t len, const char *fmt, ...) {
    if (buf == NULL || len == 0 || fmt == NULL) {
        return -1;
    }
    va_list args;
    va_start(args, fmt);
    const int written = vsnprintf(buf, len, fmt, args);
    va_end(args);
    if (written < 0 || (size_t)written >= len) {
        return -1;
    }
    return 0;
}

int ns_registry_path(char *buf, size_t len, const char *base_dir) {
    if (base_dir == NULL) {
        return -1;
    }
    return write_path(buf, len, "%s/registry.jsonl", base_dir);
}

int ns_files_index_path(char *buf, size_t len, const char *base_dir) {
    if (base_dir == NULL) {
        return -1;
    }
    return write_path(buf, len, "%s/files_index.json", base_dir);
}

int ns_acl_path(char *buf, size_t len, const char *base_dir, const char *filename) {
    if (base_dir == NULL || filename == NULL) {
        return -1;
    }
    return write_path(buf, len, "%s/acl/%s.acl.json", base_dir, filename);
}

int ns_cache_path(char *buf, size_t len, const char *base_dir) {
    if (base_dir == NULL) {
        return -1;
    }
    return write_path(buf, len, "%s/cache/lookup.cache", base_dir);
}

int ns_requests_path(char *buf, size_t len, const char *base_dir, const char *filename) {
    if (base_dir == NULL || filename == NULL) {
        return -1;
    }
    return write_path(buf, len, "%s/requests/%s.requests.json", base_dir, filename);
}

static int format_date(char *buf, size_t len, time_t when, const char *prefix) {
    if (buf == NULL || len == 0 || prefix == NULL) {
        return -1;
    }
    time_t now = when;
    if (now == 0) {
        now = time(NULL);
    }
    struct tm tm_snapshot;
    if (gmtime_r(&now, &tm_snapshot) == NULL) {
        return -1;
    }
    const int written = snprintf(
        buf,
        len,
        "%s_%04d%02d%02d.log",
        prefix,
        tm_snapshot.tm_year + 1900,
        tm_snapshot.tm_mon + 1,
        tm_snapshot.tm_mday
    );
    if (written < 0 || (size_t)written >= len) {
        return -1;
    }
    return 0;
}

int nm_log_path(char *buf, size_t len, const char *base_dir, time_t when) {
    if (base_dir == NULL) {
        return -1;
    }
    char filename[32];
    if (format_date(filename, sizeof(filename), when, "nm") != 0) {
        return -1;
    }
    return write_path(buf, len, "%s/logs/%s", base_dir, filename);
}

int ss_root_path(char *buf, size_t len, const char *base_dir, const char *ss_id) {
    if (base_dir == NULL || ss_id == NULL) {
        return -1;
    }
    return write_path(buf, len, "%s/ss_data_%s", base_dir, ss_id);
}

int ss_file_path(char *buf, size_t len, const char *base_dir, const char *ss_id, const char *filename) {
    char root[512];
    if (ss_root_path(root, sizeof(root), base_dir, ss_id) != 0) {
        return -1;
    }
    return write_path(buf, len, "%s/files/%s", root, filename);
}

int ss_metadata_path(char *buf, size_t len, const char *base_dir, const char *ss_id, const char *filename) {
    char root[512];
    if (ss_root_path(root, sizeof(root), base_dir, ss_id) != 0) {
        return -1;
    }
    return write_path(buf, len, "%s/metadata/%s.meta.json", root, filename);
}

int ss_undo_path(char *buf, size_t len, const char *base_dir, const char *ss_id, const char *filename) {
    char root[512];
    if (ss_root_path(root, sizeof(root), base_dir, ss_id) != 0) {
        return -1;
    }
    return write_path(buf, len, "%s/undo/%s.undo.json", root, filename);
}

int ss_log_path(char *buf, size_t len, const char *base_dir, const char *ss_id, time_t when) {
    char root[512];
    if (ss_root_path(root, sizeof(root), base_dir, ss_id) != 0) {
        return -1;
    }
    char filename[32];
    if (format_date(filename, sizeof(filename), when, "ss") != 0) {
        return -1;
    }
    return write_path(buf, len, "%s/logs/%s", root, filename);
}

int ss_checkpoint_dir(char *buf, size_t len, const char *base_dir, const char *ss_id, const char *filename) {
    char root[512];
    if (ss_root_path(root, sizeof(root), base_dir, ss_id) != 0) {
        return -1;
    }
    if (filename == NULL) {
        return -1;
    }
    return write_path(buf, len, "%s/checkpoints/%s", root, filename);
}

int ss_checkpoint_path(char *buf, size_t len, const char *base_dir, const char *ss_id, const char *filename, const char *tag) {
    char dir[512];
    if (ss_checkpoint_dir(dir, sizeof(dir), base_dir, ss_id, filename) != 0) {
        return -1;
    }
    if (tag == NULL) {
        return -1;
    }
    return write_path(buf, len, "%s/%s.chk", dir, tag);
}

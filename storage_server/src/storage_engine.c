#define _POSIX_C_SOURCE 200809L

#include "storage_engine.h"

#include "persistence.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

typedef struct {
    size_t start;
    size_t length;
} sentence_span_t;

typedef struct {
    pthread_mutex_t mutex;
    int locked;
} sentence_lock_t;

typedef struct storage_file {
    char filename[256];
    char owner[128];
    char *content;
    size_t content_len;
    char *undo_snapshot;
    size_t undo_len;
    char undo_token[64];
    time_t created_at;
    time_t modified_at;
    time_t last_accessed;
    char last_accessed_by[128];
    size_t word_count;
    size_t char_count;
    size_t sentence_count;
    sentence_lock_t *locks;
    size_t locks_capacity;
    pthread_mutex_t file_mutex;
} storage_file_t;

struct storage_engine {
    char root_path[PATH_MAX];
    char ss_id[128];
    storage_file_t **files;
    size_t file_count;
    size_t file_capacity;
    unsigned long undo_seq;
    pthread_mutex_t files_mutex;
};

static int ensure_dir(const char *path) {
    if (path == NULL) {
        return -1;
    }
    
    // Try to create the directory directly first
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return 0;
    }
    
    // If it failed and parent doesn't exist, create parent directories recursively
    if (errno == ENOENT) {
        char tmp[PATH_MAX];
        strncpy(tmp, path, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        
        // Find the last slash
        char *p = strrchr(tmp, '/');
        if (p == NULL || p == tmp) {
            return -1;  // No parent or root, can't help
        }
        
        // Temporarily null-terminate at the parent directory
        *p = '\0';
        
        // Recursively ensure parent exists
        if (ensure_dir(tmp) != 0) {
            return -1;
        }
        
        // Now try to create the original directory again
        if (mkdir(path, 0755) == -1 && errno != EEXIST) {
            return -1;
        }
        return 0;
    }
    
    return -1;
}

static int ensure_parent_dir(const char *path) {
    if (path == NULL) {
        return -1;
    }
    char tmp[PATH_MAX];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *slash = strrchr(tmp, '/');
    if (slash == NULL) {
        return 0;
    }
    *slash = '\0';
    if (tmp[0] == '\0') {
        return 0;
    }
    return ensure_dir(tmp);
}

static int normalize_relative_path(const char *input, char *output, size_t len) {
    if (output == NULL || len == 0) {
        return -1;
    }
    if (input == NULL) {
        output[0] = '\0';
        return 0;
    }

    char temp[PATH_MAX];
    size_t idx = 0;
    const char *p = input;

    /* Skip leading slashes */
    while (*p == '/') {
        p++;
    }

    while (*p && idx + 1 < sizeof(temp)) {
        if (*p == '/') {
            temp[idx++] = '/';
            while (*(p + 1) == '/') {
                p++;
            }
        } else {
            temp[idx++] = *p;
        }
        p++;
    }
    temp[idx] = '\0';

    /* Remove trailing slash */
    while (idx > 0 && temp[idx - 1] == '/') {
        temp[--idx] = '\0';
    }

    char copy[PATH_MAX];
    strncpy(copy, temp, sizeof(copy));
    copy[sizeof(copy) - 1] = '\0';
    char *saveptr = NULL;
    for (char *tok = strtok_r(copy, "/", &saveptr); tok != NULL; tok = strtok_r(NULL, "/", &saveptr)) {
        if (strcmp(tok, ".") == 0 || strcmp(tok, "..") == 0) {
            return -1;
        }
    }

    strncpy(output, temp, len - 1);
    output[len - 1] = '\0';
    return 0;
}

static int atomic_write(const char *path, const char *data, size_t len) {
    char tmp_path[PATH_MAX];
    if (ensure_parent_dir(path) != 0) {
        return -1;
    }
    snprintf(tmp_path, sizeof(tmp_path), "%s.XXXXXX", path);
    int fd = mkstemp(tmp_path);
    if (fd == -1) {
        return -1;
    }
    ssize_t written = write(fd, data, len);
    if (written < 0 || (size_t)written != len) {
        close(fd);
        unlink(tmp_path);
        return -1;
    }
    if (fsync(fd) == -1) {
        close(fd);
        unlink(tmp_path);
        return -1;
    }
    close(fd);
    if (rename(tmp_path, path) == -1) {
        unlink(tmp_path);
        return -1;
    }
    return 0;
}

static int is_valid_checkpoint_tag(const char *tag) {
    if (tag == NULL || *tag == '\0') {
        return 0;
    }
    for (const char *p = tag; *p; ++p) {
        if (!( (*p >= 'a' && *p <= 'z') ||
               (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9') ||
               *p == '-' || *p == '_' )) {
            return 0;
        }
    }
    return 1;
}

static void normalise_inline_whitespace(char *text) {
    if (text == NULL) {
        return;
    }
    for (char *p = text; *p; ++p) {
        if (*p == '\r' || *p == '\n' || *p == '\t') {
            *p = ' ';
        }
    }
}

static char *read_file_if_exists(const char *path, size_t *length_out) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        if (length_out) {
            *length_out = 0;
        }
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    char *buffer = calloc(1, (size_t)size + 1);
    if (buffer == NULL) {
        fclose(fp);
        return NULL;
    }
    size_t read_bytes = fread(buffer, 1, (size_t)size, fp);
    fclose(fp);
    buffer[read_bytes] = '\0';
    if (length_out) {
        *length_out = read_bytes;
    }
    return buffer;
}

static void read_folder_owner_meta(const char *folder_path, char *owner, size_t owner_len) {
    if (owner == NULL || owner_len == 0 || folder_path == NULL) {
        return;
    }
    owner[0] = '\0';
    char meta_path[PATH_MAX + 32];
    int written = snprintf(meta_path, sizeof(meta_path), "%s/.folder_meta", folder_path);
    if (written < 0 || (size_t)written >= sizeof(meta_path)) {
        return;
    }
    size_t meta_len = 0;
    char *meta = read_file_if_exists(meta_path, &meta_len);
    if (meta == NULL || meta_len == 0) {
        free(meta);
        return;
    }
    const char *owner_field = strstr(meta, "\"owner\"");
    if (owner_field != NULL) {
        owner_field = strchr(owner_field, ':');
        if (owner_field != NULL) {
            owner_field++;
            while (*owner_field == ' ' || *owner_field == '"') {
                owner_field++;
            }
            size_t idx = 0;
            while (*owner_field && *owner_field != '"' && idx + 1 < owner_len) {
                owner[idx++] = *owner_field++;
            }
            owner[idx] = '\0';
        }
    }
    free(meta);
}

static void free_sentence_spans(sentence_span_t *spans) {
    free(spans);
}

static sentence_span_t *compute_spans(const char *text, size_t len, size_t *out_count) {
    if (text == NULL || out_count == NULL) {
        return NULL;
    }
    size_t capacity = 16;
    sentence_span_t *spans = calloc(capacity, sizeof(sentence_span_t));
    if (spans == NULL) {
        return NULL;
    }

    size_t count = 0;
    size_t start = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = text[i];
        if (c == '.' || c == '!' || c == '?') {
            size_t end = i + 1;
            if (count == capacity) {
                capacity *= 2;
                sentence_span_t *tmp = realloc(spans, capacity * sizeof(sentence_span_t));
                if (tmp == NULL) {
                    free(spans);
                    return NULL;
                }
                spans = tmp;
            }
            spans[count].start = start;
            spans[count].length = end - start;
            count++;

            start = end;
            while (start < len && (text[start] == ' ' || text[start] == '\n' || text[start] == '\r' || text[start] == '\t')) {
                start++;
            }
            i = start ? start - 1 : 0;
        }
    }

    /* DO NOT count remaining text without delimiter as a sentence */
    /* Sentences are ONLY defined by ending with '.', '!', or '?' */
    /* Any text after the last delimiter is incomplete and not counted */

    *out_count = count;
    return spans;
}

static size_t count_words(const char *text) {
    if (text == NULL) {
        return 0;
    }
    size_t count = 0;
    int in_word = 0;
    for (const char *p = text; *p; ++p) {
        if (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            count++;
        }
    }
    return count;
}

static int ensure_sentence_capacity(storage_file_t *file, size_t desired) {
    if (desired <= file->locks_capacity) {
        return 0;
    }
    size_t new_capacity = file->locks_capacity ? file->locks_capacity : 4;
    while (new_capacity < desired) {
        new_capacity *= 2;
    }
    sentence_lock_t *locks = realloc(file->locks, new_capacity * sizeof(*locks));
    if (locks == NULL) {
        return -1;
    }
    for (size_t i = file->locks_capacity; i < new_capacity; ++i) {
        pthread_mutex_init(&locks[i].mutex, NULL);
        locks[i].locked = 0;
    }
    file->locks = locks;
    file->locks_capacity = new_capacity;
    return 0;
}

static storage_file_t *allocate_file(const char *filename, const char *owner) {
    storage_file_t *file = calloc(1, sizeof(*file));
    if (file == NULL) {
        return NULL;
    }
    strncpy(file->filename, filename, sizeof(file->filename) - 1);
    if (owner != NULL) {
        strncpy(file->owner, owner, sizeof(file->owner) - 1);
    } else {
        strcpy(file->owner, "unknown");
    }
    pthread_mutex_init(&file->file_mutex, NULL);
    return file;
}

static void free_file(storage_file_t *file) {
    if (file == NULL) {
        return;
    }
    free(file->content);
    free(file->undo_snapshot);
    if (file->locks != NULL) {
        for (size_t i = 0; i < file->locks_capacity; ++i) {
            pthread_mutex_destroy(&file->locks[i].mutex);
        }
        free(file->locks);
    }
    pthread_mutex_destroy(&file->file_mutex);
    free(file);
}

static void update_metadata_from_content(storage_file_t *file) {
    file->char_count = file->content_len;
    file->word_count = count_words(file->content);
    size_t span_count = 0;
    sentence_span_t *spans = compute_spans(file->content, file->content_len, &span_count);
    if (spans != NULL) {
        file->sentence_count = span_count;
        ensure_sentence_capacity(file, span_count);
        free_sentence_spans(spans);
    }
}

static int build_paths(struct storage_engine *engine,
                       const char *filename,
                       char *file_path,
                       char *metadata_path,
                       char *undo_path,
                       size_t len) {
    if (ss_file_path(file_path, len, engine->root_path, engine->ss_id, filename) != 0) {
        return -1;
    }
    if (ss_metadata_path(metadata_path, len, engine->root_path, engine->ss_id, filename) != 0) {
        return -1;
    }
    if (ss_undo_path(undo_path, len, engine->root_path, engine->ss_id, filename) != 0) {
        return -1;
    }
    return 0;
}

static int ensure_engine_dirs(struct storage_engine *engine) {
    char root[PATH_MAX];
    if (ss_root_path(root, sizeof(root), engine->root_path, engine->ss_id) != 0) {
        return -1;
    }
    if (ensure_dir(root) != 0) {
        return -1;
    }
    const char *subs[] = {"files", "metadata", "undo", "logs", "checkpoints"};
    for (size_t i = 0; i < ARRAY_SIZE(subs); ++i) {
        char path[PATH_MAX];
        int written = snprintf(path, sizeof(path), "%s/%s", root, subs[i]);
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

static int persist_metadata(struct storage_engine *engine, storage_file_t *file) {
    char file_path[PATH_MAX];
    char meta_path[PATH_MAX];
    char undo_path[PATH_MAX];
    if (build_paths(engine, file->filename, file_path, meta_path, undo_path, sizeof(file_path)) != 0) {
        return -1;
    }
    char buffer[512];
    int written = snprintf(buffer, sizeof(buffer),
                           "{\"filename\":\"%s\",\"owner\":\"%s\"," 
                           "\"word_count\":%zu,\"char_count\":%zu,\"sentence_count\":%zu,"
                           "\"created_at\":%lld,\"modified_at\":%lld,\"last_accessed\":%lld,"
                           "\"last_accessed_by\":\"%s\",\"undo_token\":\"%s\"}",
                           file->filename,
                           file->owner,
                           file->word_count,
                           file->char_count,
                           file->sentence_count,
                           (long long)file->created_at,
                           (long long)file->modified_at,
                           (long long)file->last_accessed,
                           file->last_accessed_by,
                           file->undo_token);
    if (written < 0 || (size_t)written >= sizeof(buffer)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return atomic_write(meta_path, buffer, (size_t)written);
}

static int persist_content(struct storage_engine *engine, storage_file_t *file) {
    char file_path[PATH_MAX];
    char meta_path[PATH_MAX];
    char undo_path[PATH_MAX];
    if (build_paths(engine, file->filename, file_path, meta_path, undo_path, sizeof(file_path)) != 0) {
        return -1;
    }
    return atomic_write(file_path, file->content != NULL ? file->content : "", file->content_len);
}

static int persist_undo(struct storage_engine *engine, storage_file_t *file) {
    char file_path[PATH_MAX];
    char meta_path[PATH_MAX];
    char undo_path[PATH_MAX];
    if (build_paths(engine, file->filename, file_path, meta_path, undo_path, sizeof(file_path)) != 0) {
        return -1;
    }
    if (file->undo_snapshot == NULL) {
        unlink(undo_path);
        return 0;
    }
    return atomic_write(undo_path, file->undo_snapshot, file->undo_len);
}

static void generate_undo_token(struct storage_engine *engine, storage_file_t *file) {
    engine->undo_seq++;
    snprintf(file->undo_token, sizeof(file->undo_token), "undo-%06lu", engine->undo_seq);
}

static storage_file_t *find_file_locked(struct storage_engine *engine, const char *filename) {
    for (size_t i = 0; i < engine->file_count; ++i) {
        if (strcmp(engine->files[i]->filename, filename) == 0) {
            return engine->files[i];
        }
    }
    return NULL;
}

static int load_metadata(storage_file_t *file, const char *metadata_json) {
    if (metadata_json == NULL) {
        time_t now = time(NULL);
        file->created_at = now;
        file->modified_at = now;
        file->last_accessed = 0;
        return 0;
    }
    const char *owner = strstr(metadata_json, "\"owner\"");
    if (owner != NULL) {
        owner = strchr(owner, ':');
        if (owner != NULL) {
            owner++;
            while (*owner == ' ' || *owner == '"') {
                owner++;
            }
            size_t idx = 0;
            while (*owner && *owner != '"' && idx < sizeof(file->owner) - 1) {
                file->owner[idx++] = *owner++;
            }
            file->owner[idx] = '\0';
        }
    }
    const char *fields[] = {"word_count", "char_count", "sentence_count", "created_at", "modified_at", "last_accessed"};
    size_t *targets[] = {&file->word_count, &file->char_count, &file->sentence_count, NULL, NULL, NULL};
    for (size_t i = 0; i < 3; ++i) {
        const char *pos = strstr(metadata_json, fields[i]);
        if (pos != NULL) {
            pos = strchr(pos, ':');
            if (pos != NULL) {
                pos++;
                *targets[i] = strtoull(pos, NULL, 10);
            }
        }
    }
    const char *times[] = {"created_at", "modified_at", "last_accessed"};
    time_t *time_targets[] = {&file->created_at, &file->modified_at, &file->last_accessed};
    for (size_t i = 0; i < 3; ++i) {
        const char *pos = strstr(metadata_json, times[i]);
        if (pos != NULL) {
            pos = strchr(pos, ':');
            if (pos != NULL) {
                pos++;
                long long value = strtoll(pos, NULL, 10);
                *time_targets[i] = (time_t)value;
            }
        }
    }
    const char *undo = strstr(metadata_json, "undo_token");
    if (undo != NULL) {
        undo = strchr(undo, ':');
        if (undo != NULL) {
            undo++;
            while (*undo == ' ' || *undo == '"') {
                undo++;
            }
            size_t idx = 0;
            while (*undo && *undo != '"' && idx < sizeof(file->undo_token) - 1) {
                file->undo_token[idx++] = *undo++;
            }
            file->undo_token[idx] = '\0';
        }
    }

    const char *last_by = strstr(metadata_json, "last_accessed_by");
    if (last_by != NULL) {
        last_by = strchr(last_by, ':');
        if (last_by != NULL) {
            last_by++;
            while (*last_by == ' ' || *last_by == '"') {
                last_by++;
            }
            size_t idx = 0;
            while (*last_by && *last_by != '"' && idx < sizeof(file->last_accessed_by) - 1) {
                file->last_accessed_by[idx++] = *last_by++;
            }
            file->last_accessed_by[idx] = '\0';
        }
    }
    return 0;
}

static int load_file_from_disk(struct storage_engine *engine, storage_file_t *file) {
    char file_path[PATH_MAX];
    char meta_path[PATH_MAX];
    char undo_path[PATH_MAX];
    if (build_paths(engine, file->filename, file_path, meta_path, undo_path, sizeof(file_path)) != 0) {
        return -1;
    }
    file->content = read_file_if_exists(file_path, &file->content_len);
    if (file->content == NULL) {
        file->content = calloc(1, 1);
        file->content_len = 0;
    }
    char *meta = read_file_if_exists(meta_path, NULL);
    load_metadata(file, meta);
    free(meta);
    file->undo_snapshot = read_file_if_exists(undo_path, &file->undo_len);
    update_metadata_from_content(file);
    return 0;
}

int storage_engine_init(storage_engine_t **engine_out, const char *base_dir, const char *ss_id) {
    if (engine_out == NULL || base_dir == NULL || ss_id == NULL) {
        errno = EINVAL;
        return -1;
    }
    struct storage_engine *engine = calloc(1, sizeof(*engine));
    if (engine == NULL) {
        return -1;
    }
    strncpy(engine->root_path, base_dir, sizeof(engine->root_path) - 1);
    strncpy(engine->ss_id, ss_id, sizeof(engine->ss_id) - 1);
    pthread_mutex_init(&engine->files_mutex, NULL);
    if (ensure_engine_dirs(engine) != 0) {
        storage_engine_destroy(engine);
        return -1;
    }
    *engine_out = engine;
    return 0;
}

void storage_engine_destroy(storage_engine_t *engine) {
    if (engine == NULL) {
        return;
    }
    if (engine->files != NULL) {
        for (size_t i = 0; i < engine->file_count; ++i) {
            free_file(engine->files[i]);
        }
        free(engine->files);
    }
    pthread_mutex_destroy(&engine->files_mutex);
    free(engine);
}

static storage_file_t *get_or_load_file(storage_engine_t *engine, const char *filename, const char *owner_hint) {
    pthread_mutex_lock(&engine->files_mutex);
    storage_file_t *file = find_file_locked(engine, filename);
    if (file != NULL) {
        pthread_mutex_unlock(&engine->files_mutex);
        return file;
    }
    file = allocate_file(filename, owner_hint);
    if (file == NULL) {
        pthread_mutex_unlock(&engine->files_mutex);
        return NULL;
    }
    if (load_file_from_disk(engine, file) != 0) {
        free_file(file);
        pthread_mutex_unlock(&engine->files_mutex);
        return NULL;
    }
    if (engine->file_count == engine->file_capacity) {
        size_t new_capacity = engine->file_capacity ? engine->file_capacity * 2 : 4;
        storage_file_t **tmp = realloc(engine->files, new_capacity * sizeof(*tmp));
        if (tmp == NULL) {
            free_file(file);
            pthread_mutex_unlock(&engine->files_mutex);
            return NULL;
        }
        engine->files = tmp;
        engine->file_capacity = new_capacity;
    }
    engine->files[engine->file_count++] = file;
    pthread_mutex_unlock(&engine->files_mutex);
    return file;
}

static int write_new_content(storage_engine_t *engine,
                             storage_file_t *file,
                             const sentence_span_t *span,
                             const char *replacement,
                             size_t replacement_len) {
    size_t prefix_len = span->start;
    size_t suffix_offset = span->start + span->length;
    size_t suffix_len = file->content_len > suffix_offset ? file->content_len - suffix_offset : 0;

    size_t new_len = prefix_len + replacement_len + suffix_len;
    char *new_content = calloc(1, new_len + 1);
    if (new_content == NULL) {
        return -1;
    }
    if (prefix_len > 0) {
        memcpy(new_content, file->content, prefix_len);
    }
    if (replacement_len > 0) {
        memcpy(new_content + prefix_len, replacement, replacement_len);
    }
    if (suffix_len > 0) {
        memcpy(new_content + prefix_len + replacement_len, file->content + suffix_offset, suffix_len);
    }
    new_content[new_len] = '\0';

    free(file->undo_snapshot);
    file->undo_snapshot = file->content;
    file->undo_len = file->content_len;
    file->content = new_content;
    file->content_len = new_len;
    generate_undo_token(engine, file);
    return 0;
}

static int tokenize_words(const char *sentence, char ***out_words, size_t *out_count) {
    if (sentence == NULL || out_words == NULL || out_count == NULL) {
        return -1;
    }
    char *copy = strdup(sentence);
    if (copy == NULL) {
        return -1;
    }
    normalise_inline_whitespace(copy);
    size_t capacity = 8;
    size_t count = 0;
    char **words = calloc(capacity, sizeof(char *));
    if (words == NULL) {
        free(copy);
        return -1;
    }
    char *saveptr = NULL;
    char *token = strtok_r(copy, " ", &saveptr);
    while (token != NULL) {
        if (*token != '\0') {
            if (count == capacity) {
                capacity *= 2;
                char **tmp = realloc(words, capacity * sizeof(char *));
                if (tmp == NULL) {
                    for (size_t i = 0; i < count; ++i) {
                        free(words[i]);
                    }
                    free(words);
                    free(copy);
                    return -1;
                }
                words = tmp;
            }
            words[count++] = strdup(token);
        }
        token = strtok_r(NULL, " ", &saveptr);
    }
    free(copy);
    *out_words = words;
    *out_count = count;
    return 0;
}

static void free_words(char **words, size_t count) {
    if (words == NULL) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        free(words[i]);
    }
    free(words);
}

static int apply_updates(char ***words_ptr, size_t *count_ptr, const storage_word_update_t *updates, size_t update_count) {
    char **words = *words_ptr;
    size_t count = *count_ptr;
    for (size_t i = 0; i < update_count; ++i) {
        const storage_word_update_t *upd = &updates[i];
        if (upd->text == NULL) {
            return STORAGE_ERR_INVALID;
        }
        if (upd->index > count) {
            return STORAGE_ERR_INVALID;
        }
        char *tmp = strdup(upd->text);
        if (tmp == NULL) {
            return STORAGE_ERR_IO;
        }
        normalise_inline_whitespace(tmp);
        size_t capacity = 4;
        size_t new_count = 0;
        char **new_tokens = calloc(capacity, sizeof(char *));
        if (new_tokens == NULL) {
            free(tmp);
            return STORAGE_ERR_IO;
        }
        char *saveptr = NULL;
    char *token = strtok_r(tmp, " ", &saveptr);
        while (token != NULL) {
            if (*token != '\0') {
                if (new_count == capacity) {
                    capacity *= 2;
                    char **tmp_tokens = realloc(new_tokens, capacity * sizeof(char *));
                    if (tmp_tokens == NULL) {
                        for (size_t j = 0; j < new_count; ++j) {
                            free(new_tokens[j]);
                        }
                        free(new_tokens);
                        free(tmp);
                        return STORAGE_ERR_IO;
                    }
                    new_tokens = tmp_tokens;
                }
                new_tokens[new_count++] = strdup(token);
            }
            token = strtok_r(NULL, " ", &saveptr);
        }
        free(tmp);
        if (new_count == 0) {
            continue;
        }
        char **expanded = realloc(words, (count + new_count) * sizeof(char *));
        if (expanded == NULL) {
            for (size_t j = 0; j < new_count; ++j) {
                free(new_tokens[j]);
            }
            free(new_tokens);
            return STORAGE_ERR_IO;
        }
        words = expanded;
        memmove(&words[upd->index + new_count], &words[upd->index], (count - upd->index) * sizeof(char *));
        for (size_t j = 0; j < new_count; ++j) {
            words[upd->index + j] = new_tokens[j];
        }
        count += new_count;
        free(new_tokens);
    }
    *words_ptr = words;
    *count_ptr = count;
    return STORAGE_OK;
}

static char *join_words(char **words, size_t count, size_t *out_len) {
    size_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        total += strlen(words[i]);
        if (i + 1 < count) {
            total += 1;
        }
    }
    char *result = calloc(1, total + 1);
    if (result == NULL) {
        return NULL;
    }
    size_t offset = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t len = strlen(words[i]);
        memcpy(result + offset, words[i], len);
        offset += len;
        if (i + 1 < count) {
            result[offset++] = ' ';
        }
    }
    result[offset] = '\0';
    if (out_len != NULL) {
        *out_len = offset;
    }
    return result;
}

int storage_engine_create_file(storage_engine_t *engine, const char *filename, const char *owner) {
    if (engine == NULL || filename == NULL || owner == NULL) {
        return STORAGE_ERR_INVALID;
    }
    storage_file_t *file = get_or_load_file(engine, filename, owner);
    if (file == NULL) {
        return STORAGE_ERR_IO;
    }
    pthread_mutex_lock(&file->file_mutex);
    if (file->content_len > 0) {
        pthread_mutex_unlock(&file->file_mutex);
        return STORAGE_ERR_INVALID;
    }
    time_t now = time(NULL);
    file->created_at = now;
    file->modified_at = now;
    file->last_accessed = now;
    strncpy(file->owner, owner, sizeof(file->owner) - 1);
    update_metadata_from_content(file);
    if (persist_content(engine, file) != 0 || persist_metadata(engine, file) != 0) {
        pthread_mutex_unlock(&file->file_mutex);
        return STORAGE_ERR_IO;
    }
    pthread_mutex_unlock(&file->file_mutex);
    return STORAGE_OK;
}

int storage_engine_delete_file(storage_engine_t *engine, const char *filename) {
    if (engine == NULL || filename == NULL) {
        return STORAGE_ERR_INVALID;
    }
    pthread_mutex_lock(&engine->files_mutex);
    storage_file_t *file = find_file_locked(engine, filename);
    if (file == NULL) {
        pthread_mutex_unlock(&engine->files_mutex);
        return STORAGE_ERR_NOT_FOUND;
    }
    size_t index = 0;
    for (; index < engine->file_count; ++index) {
        if (engine->files[index] == file) {
            break;
        }
    }
    for (size_t i = index; i + 1 < engine->file_count; ++i) {
        engine->files[i] = engine->files[i + 1];
    }
    engine->file_count--;
    pthread_mutex_unlock(&engine->files_mutex);

    char file_path[PATH_MAX];
    char meta_path[PATH_MAX];
    char undo_path[PATH_MAX];
    if (build_paths(engine, filename, file_path, meta_path, undo_path, sizeof(file_path)) == 0) {
        unlink(file_path);
        unlink(meta_path);
        unlink(undo_path);
    }
    free_file(file);
    return STORAGE_OK;
}

int storage_engine_read(storage_engine_t *engine, const char *filename, char **content_out) {
    if (engine == NULL || filename == NULL || content_out == NULL) {
        return STORAGE_ERR_INVALID;
    }
    storage_file_t *file = get_or_load_file(engine, filename, NULL);
    if (file == NULL) {
        return STORAGE_ERR_NOT_FOUND;
    }
    pthread_mutex_lock(&file->file_mutex);
    *content_out = strdup(file->content);
    file->last_accessed = time(NULL);
    persist_metadata(engine, file);
    pthread_mutex_unlock(&file->file_mutex);
    return *content_out != NULL ? STORAGE_OK : STORAGE_ERR_IO;
}

int storage_engine_write(storage_engine_t *engine,
                         const char *filename,
                         size_t sentence_index,
                         const storage_word_update_t *updates,
                         size_t update_count,
                         char *undo_token_buf,
                         size_t undo_token_len) {
    if (engine == NULL || filename == NULL || updates == NULL || update_count == 0) {
        return STORAGE_ERR_INVALID;
    }
    storage_file_t *file = get_or_load_file(engine, filename, NULL);
    if (file == NULL) {
        return STORAGE_ERR_NOT_FOUND;
    }
    pthread_mutex_lock(&file->file_mutex);

    size_t span_count = 0;
    sentence_span_t temp_span;
    sentence_span_t *spans = NULL;
    if (file->content_len > 0) {
        spans = compute_spans(file->content, file->content_len, &span_count);
        if (spans == NULL) {
            pthread_mutex_unlock(&file->file_mutex);
            return STORAGE_ERR_IO;
        }
    } else {
        span_count = 0;
    }

    const sentence_span_t *span = NULL;
    /* Allow editing incomplete text (sentence_index == span_count when there's content but no delimiter) */
    if (span_count == 0 && sentence_index == 0) {
        /* Empty file or incomplete text - create span for entire content */
        if (ensure_sentence_capacity(file, 1) != 0) {
            pthread_mutex_unlock(&file->file_mutex);
            return STORAGE_ERR_IO;
        }
        temp_span.start = 0;
        temp_span.length = file->content_len;
        span = &temp_span;
        span_count = 1;
    } else if (sentence_index == span_count && file->content_len > 0) {
        /* Editing incomplete text at the end - find where last sentence ended */
        if (ensure_sentence_capacity(file, span_count + 1) != 0) {
            free_sentence_spans(spans);
            pthread_mutex_unlock(&file->file_mutex);
            return STORAGE_ERR_IO;
        }
        /* Find start of incomplete text (after last sentence) */
        size_t incomplete_start = 0;
        if (span_count > 0) {
            incomplete_start = spans[span_count - 1].start + spans[span_count - 1].length;
            /* Skip whitespace */
            while (incomplete_start < file->content_len && 
                   (file->content[incomplete_start] == ' ' || 
                    file->content[incomplete_start] == '\t' || 
                    file->content[incomplete_start] == '\n')) {
                incomplete_start++;
            }
        }
        temp_span.start = incomplete_start;
        temp_span.length = file->content_len - incomplete_start;
        span = &temp_span;
    } else if (sentence_index >= span_count) {
        free_sentence_spans(spans);
        pthread_mutex_unlock(&file->file_mutex);
        return STORAGE_ERR_INVALID;
    } else {
        if (ensure_sentence_capacity(file, span_count) != 0) {
            free_sentence_spans(spans);
            pthread_mutex_unlock(&file->file_mutex);
            return STORAGE_ERR_IO;
        }
        span = &spans[sentence_index];
    }

    sentence_lock_t *lock = &file->locks[sentence_index];
    if (pthread_mutex_trylock(&lock->mutex) != 0) {
        free_sentence_spans(spans);
        pthread_mutex_unlock(&file->file_mutex);
        return STORAGE_ERR_LOCKED;
    }
    lock->locked = 1;

    char *sentence = calloc(1, span->length + 1);
    if (sentence == NULL) {
        pthread_mutex_unlock(&lock->mutex);
        free_sentence_spans(spans);
        pthread_mutex_unlock(&file->file_mutex);
        return STORAGE_ERR_IO;
    }
    memcpy(sentence, file->content + span->start, span->length);
    sentence[span->length] = '\0';

    char **words = NULL;
    size_t word_count = 0;
    if (tokenize_words(sentence, &words, &word_count) != 0) {
        free(sentence);
        pthread_mutex_unlock(&lock->mutex);
        free_sentence_spans(spans);
        pthread_mutex_unlock(&file->file_mutex);
        return STORAGE_ERR_IO;
    }

    int rc = apply_updates(&words, &word_count, updates, update_count);
    if (rc != STORAGE_OK) {
        free_words(words, word_count);
        free(sentence);
        pthread_mutex_unlock(&lock->mutex);
        free_sentence_spans(spans);
        pthread_mutex_unlock(&file->file_mutex);
        return rc;
    }

    size_t new_sentence_len = 0;
    char *new_sentence = join_words(words, word_count, &new_sentence_len);
    free_words(words, word_count);
    free(sentence);
    if (new_sentence == NULL) {
        pthread_mutex_unlock(&lock->mutex);
        free_sentence_spans(spans);
        pthread_mutex_unlock(&file->file_mutex);
        return STORAGE_ERR_IO;
    }

    if (write_new_content(engine, file, span, new_sentence, new_sentence_len) != 0) {
        free(new_sentence);
        pthread_mutex_unlock(&lock->mutex);
        free_sentence_spans(spans);
        pthread_mutex_unlock(&file->file_mutex);
        return STORAGE_ERR_IO;
    }
    free(new_sentence);

    free_sentence_spans(spans);

    file->modified_at = time(NULL);
    file->last_accessed = file->modified_at;
    update_metadata_from_content(file);
    persist_content(engine, file);
    persist_metadata(engine, file);
    persist_undo(engine, file);

    if (undo_token_buf != NULL && undo_token_len > 0) {
        strncpy(undo_token_buf, file->undo_token, undo_token_len - 1);
        undo_token_buf[undo_token_len - 1] = '\0';
    }

    lock->locked = 0;
    pthread_mutex_unlock(&lock->mutex);
    pthread_mutex_unlock(&file->file_mutex);
    return STORAGE_OK;
}

int storage_engine_unlock_sentence(storage_engine_t *engine, const char *filename, size_t sentence_index) {
    if (engine == NULL || filename == NULL) {
        return STORAGE_ERR_INVALID;
    }
    storage_file_t *file = get_or_load_file(engine, filename, NULL);
    if (file == NULL) {
        return STORAGE_ERR_NOT_FOUND;
    }
    if (sentence_index >= file->locks_capacity) {
        return STORAGE_ERR_INVALID;
    }
    pthread_mutex_lock(&file->file_mutex);
    sentence_lock_t *lock = &file->locks[sentence_index];
    if (lock->locked) {
        lock->locked = 0;
        pthread_mutex_unlock(&lock->mutex);
    }
    pthread_mutex_unlock(&file->file_mutex);
    return STORAGE_OK;
}

int storage_engine_undo(storage_engine_t *engine, const char *filename) {
    if (engine == NULL || filename == NULL) {
        return STORAGE_ERR_INVALID;
    }
    storage_file_t *file = get_or_load_file(engine, filename, NULL);
    if (file == NULL) {
        return STORAGE_ERR_NOT_FOUND;
    }
    pthread_mutex_lock(&file->file_mutex);
    if (file->undo_snapshot == NULL) {
        pthread_mutex_unlock(&file->file_mutex);
        return STORAGE_ERR_INVALID;
    }
    char *current = file->content;
    size_t current_len = file->content_len;
    file->content = file->undo_snapshot;
    file->content_len = file->undo_len;
    file->undo_snapshot = current;
    file->undo_len = current_len;
    file->modified_at = time(NULL);
    file->last_accessed = file->modified_at;
    update_metadata_from_content(file);
    persist_content(engine, file);
    persist_metadata(engine, file);
    persist_undo(engine, file);
    pthread_mutex_unlock(&file->file_mutex);
    return STORAGE_OK;
}

int storage_engine_info(storage_engine_t *engine, const char *filename, storage_file_info_t *info_out) {
    if (engine == NULL || filename == NULL || info_out == NULL) {
        return STORAGE_ERR_INVALID;
    }
    storage_file_t *file = get_or_load_file(engine, filename, NULL);
    if (file == NULL) {
        return STORAGE_ERR_NOT_FOUND;
    }
    pthread_mutex_lock(&file->file_mutex);
    strncpy(info_out->filename, file->filename, sizeof(info_out->filename) - 1);
    info_out->filename[sizeof(info_out->filename) - 1] = '\0';
    strncpy(info_out->owner, file->owner, sizeof(info_out->owner) - 1);
    info_out->owner[sizeof(info_out->owner) - 1] = '\0';
    info_out->word_count = file->word_count;
    info_out->char_count = file->char_count;
    info_out->sentence_count = file->sentence_count;
    info_out->created_at = file->created_at;
    info_out->modified_at = file->modified_at;
    info_out->last_accessed = file->last_accessed;
    strncpy(info_out->last_accessed_by, file->last_accessed_by, sizeof(info_out->last_accessed_by) - 1);
    info_out->last_accessed_by[sizeof(info_out->last_accessed_by) - 1] = '\0';
    pthread_mutex_unlock(&file->file_mutex);
    return STORAGE_OK;
}

int storage_engine_mark_access(storage_engine_t *engine, const char *filename, const char *username) {
    if (engine == NULL || filename == NULL) {
        return STORAGE_ERR_INVALID;
    }
    storage_file_t *file = get_or_load_file(engine, filename, NULL);
    if (file == NULL) {
        return STORAGE_ERR_NOT_FOUND;
    }
    pthread_mutex_lock(&file->file_mutex);
    file->last_accessed = time(NULL);
    if (username != NULL && username[0] != '\0') {
        strncpy(file->last_accessed_by, username, sizeof(file->last_accessed_by) - 1);
        file->last_accessed_by[sizeof(file->last_accessed_by) - 1] = '\0';
    }
    int persist_rc = persist_metadata(engine, file);
    pthread_mutex_unlock(&file->file_mutex);
    return persist_rc == 0 ? STORAGE_OK : STORAGE_ERR_IO;
}

int storage_engine_create_folder(storage_engine_t *engine, const char *foldername, const char *owner) {
    if (engine == NULL || foldername == NULL || foldername[0] == '\0') {
        return STORAGE_ERR_INVALID;
    }
    char relative[PATH_MAX];
    if (normalize_relative_path(foldername, relative, sizeof(relative)) != 0 || relative[0] == '\0') {
        return STORAGE_ERR_INVALID;
    }
    
    // Build folder path
    char folder_path[PATH_MAX];
    if (ss_file_path(folder_path, sizeof(folder_path), engine->root_path, engine->ss_id, relative) != 0) {
        return STORAGE_ERR_INVALID;
    }
    
    // Check if folder already exists
    struct stat st;
    if (stat(folder_path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return STORAGE_ERR_EXISTS; // Folder already exists
        } else {
            return STORAGE_ERR_INVALID; // Path exists but is not a folder
        }
    }
    
    // Create the folder
    if (ensure_dir(folder_path) != 0) {
        return STORAGE_ERR_IO;
    }
    
    // Create a metadata file for the folder to store ownership
    // Use a special marker file: .folder_meta
    char meta_path[PATH_MAX + 20];  // Extra space for "/.folder_meta"
    int len = snprintf(meta_path, sizeof(meta_path), "%s/.folder_meta", folder_path);
    if (len > 0 && (size_t)len < sizeof(meta_path)) {
        FILE *meta_file = fopen(meta_path, "w");
        if (meta_file != NULL) {
            fprintf(meta_file, "{\"owner\":\"%s\",\"type\":\"folder\"}\n", owner ? owner : "system");
            fclose(meta_file);
        }
    }
    
    return STORAGE_OK;
}

int storage_engine_move_file(storage_engine_t *engine, const char *filename, const char *foldername) {
    if (engine == NULL || filename == NULL || foldername == NULL) {
        return STORAGE_ERR_INVALID;
    }

    char src_rel[PATH_MAX];
    if (normalize_relative_path(filename, src_rel, sizeof(src_rel)) != 0 || src_rel[0] == '\0') {
        return STORAGE_ERR_INVALID;
    }
    char folder_rel[PATH_MAX];
    if (normalize_relative_path(foldername, folder_rel, sizeof(folder_rel)) != 0) {
        return STORAGE_ERR_INVALID;
    }
    
    // Build source paths
    char src_file[PATH_MAX], src_meta[PATH_MAX], src_undo[PATH_MAX];
    if (build_paths(engine, src_rel, src_file, src_meta, src_undo, sizeof(src_file)) != 0) {
        return STORAGE_ERR_INVALID;
    }
    
    // Check if source file exists
    struct stat st;
    if (stat(src_file, &st) != 0) {
        return STORAGE_ERR_NOT_FOUND;
    }
    
    // Build destination folder path
    char folder_path[PATH_MAX];
    if (ss_file_path(folder_path, sizeof(folder_path), engine->root_path, engine->ss_id, folder_rel) != 0) {
        return STORAGE_ERR_INVALID;
    }
    
    // Check if destination folder exists
    if (stat(folder_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return STORAGE_ERR_NOT_FOUND;
    }
    
    // Build destination file path (folder/filename)
    const char *leaf = strrchr(src_rel, '/');
    leaf = (leaf != NULL) ? leaf + 1 : src_rel;

    char dest_rel[PATH_MAX];
    if (folder_rel[0] == '\0') {
        if (strlen(leaf) >= sizeof(dest_rel)) {
            return STORAGE_ERR_INVALID;
        }
        snprintf(dest_rel, sizeof(dest_rel), "%s", leaf);
    } else {
        int written = snprintf(dest_rel, sizeof(dest_rel), "%s/%s", folder_rel, leaf);
        if (written < 0 || (size_t)written >= sizeof(dest_rel)) {
            return STORAGE_ERR_INVALID;
        }
    }
    
    char dest_file[PATH_MAX], dest_meta[PATH_MAX], dest_undo[PATH_MAX];
    if (build_paths(engine, dest_rel, dest_file, dest_meta, dest_undo, sizeof(dest_file)) != 0) {
        return STORAGE_ERR_INVALID;
    }
    
    // Ensure destination metadata and undo directories exist
    char dest_meta_dir[PATH_MAX], dest_undo_dir[PATH_MAX];
    snprintf(dest_meta_dir, sizeof(dest_meta_dir), "%s", dest_meta);
    snprintf(dest_undo_dir, sizeof(dest_undo_dir), "%s", dest_undo);
    char *last_slash = strrchr(dest_meta_dir, '/');
    if (last_slash) *last_slash = '\0';
    last_slash = strrchr(dest_undo_dir, '/');
    if (last_slash) *last_slash = '\0';
    ensure_dir(dest_meta_dir);
    ensure_dir(dest_undo_dir);
    
    // Move files (file, metadata, undo)
    if (rename(src_file, dest_file) != 0) {
        return STORAGE_ERR_IO;
    }
    
    // Move metadata (ignore error if doesn't exist)
    rename(src_meta, dest_meta);
    
    // Move undo file (ignore error if doesn't exist)
    rename(src_undo, dest_undo);
    
    // Update in-memory file record if loaded
    pthread_mutex_lock(&engine->files_mutex);
    storage_file_t *file = find_file_locked(engine, filename);
    if (file != NULL) {
        pthread_mutex_lock(&file->file_mutex);
        size_t dest_len = strlen(dest_rel);
        if (dest_len < sizeof(file->filename)) {
            strncpy(file->filename, dest_rel, sizeof(file->filename) - 1);
            file->filename[sizeof(file->filename) - 1] = '\0';
        }
        pthread_mutex_unlock(&file->file_mutex);
    }
    pthread_mutex_unlock(&engine->files_mutex);
    
    return STORAGE_OK;
}

int storage_engine_list_folder(storage_engine_t *engine,
                              const char *foldername,
                              const char *username,
                              char ***files_out,
                              size_t *file_count_out,
                              char ***folders_out,
                              size_t *folder_count_out) {
    if (engine == NULL || files_out == NULL || file_count_out == NULL ||
        folders_out == NULL || folder_count_out == NULL) {
        return STORAGE_ERR_INVALID;
    }

    *files_out = NULL;
    *folders_out = NULL;
    *file_count_out = 0;
    *folder_count_out = 0;

    char folder_rel[PATH_MAX];
    if (foldername == NULL || foldername[0] == '\0' || strcmp(foldername, "/") == 0) {
        folder_rel[0] = '\0';
    } else if (normalize_relative_path(foldername, folder_rel, sizeof(folder_rel)) != 0) {
        return STORAGE_ERR_INVALID;
    }

    char folder_path[PATH_MAX];
    if (ss_file_path(folder_path, sizeof(folder_path), engine->root_path, engine->ss_id, folder_rel) != 0) {
        return STORAGE_ERR_INVALID;
    }

    struct stat st;
    if (stat(folder_path, &st) != 0) {
        return STORAGE_ERR_NOT_FOUND;
    }
    if (!S_ISDIR(st.st_mode)) {
        return STORAGE_ERR_NOT_FOLDER;
    }

    if (username != NULL && username[0] != '\0' && folder_rel[0] != '\0') {
        char owner[128];
        read_folder_owner_meta(folder_path, owner, sizeof(owner));
        if (owner[0] != '\0' && strcmp(owner, username) != 0) {
            return STORAGE_ERR_NO_ACCESS;
        }
    }

    DIR *dir = opendir(folder_path);
    if (dir == NULL) {
        return STORAGE_ERR_IO;
    }

    size_t file_count = 0;
    size_t folder_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (strcmp(entry->d_name, ".folder_meta") == 0) {
            continue;
        }
        char entry_path[PATH_MAX];
        int written = snprintf(entry_path, sizeof(entry_path), "%s/%s", folder_path, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(entry_path)) {
            continue;
        }
        if (stat(entry_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            folder_count++;
        } else {
            file_count++;
        }
    }

    char **files = NULL;
    char **folders = NULL;
    if (file_count > 0) {
        files = calloc(file_count, sizeof(char *));
        if (files == NULL) {
            closedir(dir);
            return STORAGE_ERR_IO;
        }
    }
    if (folder_count > 0) {
        folders = calloc(folder_count, sizeof(char *));
        if (folders == NULL) {
            if (files != NULL) {
                free(files);
            }
            closedir(dir);
            return STORAGE_ERR_IO;
        }
    }

    rewinddir(dir);
    size_t file_idx = 0;
    size_t folder_idx = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (strcmp(entry->d_name, ".folder_meta") == 0) {
            continue;
        }
        char entry_path[PATH_MAX];
        int written = snprintf(entry_path, sizeof(entry_path), "%s/%s", folder_path, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(entry_path)) {
            continue;
        }
        int is_dir = (stat(entry_path, &st) == 0 && S_ISDIR(st.st_mode));
        char **target_array = is_dir ? folders : files;
        size_t *target_idx = is_dir ? &folder_idx : &file_idx;
        size_t target_cap = is_dir ? folder_count : file_count;
        if (target_array == NULL || *target_idx >= target_cap) {
            continue;
        }
        target_array[*target_idx] = strdup(entry->d_name);
        if (target_array[*target_idx] == NULL) {
            // free previously allocated entries before returning
            for (size_t i = 0; i < file_idx; ++i) {
                free(files[i]);
            }
            for (size_t i = 0; i < folder_idx; ++i) {
                free(folders[i]);
            }
            free(files);
            free(folders);
            closedir(dir);
            return STORAGE_ERR_IO;
        }
        (*target_idx)++;
    }

    closedir(dir);
    *files_out = files;
    *file_count_out = file_count;
    *folders_out = folders;
    *folder_count_out = folder_count;
    return STORAGE_OK;
}

int storage_engine_checkpoint(storage_engine_t *engine, const char *filename, const char *tag) {
    if (engine == NULL || filename == NULL || tag == NULL) {
        return STORAGE_ERR_INVALID;
    }
    if (!is_valid_checkpoint_tag(tag)) {
        return STORAGE_ERR_INVALID;
    }
    storage_file_t *file = get_or_load_file(engine, filename, NULL);
    if (file == NULL) {
        return STORAGE_ERR_NOT_FOUND;
    }

    pthread_mutex_lock(&file->file_mutex);
    char checkpoint_dir[PATH_MAX];
    if (ss_checkpoint_dir(checkpoint_dir, sizeof(checkpoint_dir), engine->root_path, engine->ss_id, filename) != 0) {
        pthread_mutex_unlock(&file->file_mutex);
        return STORAGE_ERR_IO;
    }
    if (ensure_dir(checkpoint_dir) != 0) {
        pthread_mutex_unlock(&file->file_mutex);
        return STORAGE_ERR_IO;
    }

    char checkpoint_path[PATH_MAX];
    if (ss_checkpoint_path(checkpoint_path, sizeof(checkpoint_path), engine->root_path, engine->ss_id, filename, tag) != 0) {
        pthread_mutex_unlock(&file->file_mutex);
        return STORAGE_ERR_IO;
    }
    if (access(checkpoint_path, F_OK) == 0) {
        pthread_mutex_unlock(&file->file_mutex);
        return STORAGE_ERR_EXISTS;
    }

    int rc = atomic_write(checkpoint_path, file->content != NULL ? file->content : "", file->content_len);
    pthread_mutex_unlock(&file->file_mutex);
    return rc == 0 ? STORAGE_OK : STORAGE_ERR_IO;
}

int storage_engine_list_checkpoints(storage_engine_t *engine, const char *filename, char ***tags_out, size_t *count_out) {
    if (engine == NULL || filename == NULL || tags_out == NULL || count_out == NULL) {
        return STORAGE_ERR_INVALID;
    }
    *tags_out = NULL;
    *count_out = 0;

    char checkpoint_dir[PATH_MAX];
    if (ss_checkpoint_dir(checkpoint_dir, sizeof(checkpoint_dir), engine->root_path, engine->ss_id, filename) != 0) {
        return STORAGE_ERR_IO;
    }

    DIR *dir = opendir(checkpoint_dir);
    if (dir == NULL) {
        if (errno == ENOENT) {
            return STORAGE_OK;
        }
        return STORAGE_ERR_IO;
    }

    size_t capacity = 8;
    char **tags = calloc(capacity, sizeof(char *));
    if (tags == NULL) {
        closedir(dir);
        return STORAGE_ERR_IO;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        const char *name = entry->d_name;
        size_t len = strlen(name);
        if (len < 5 || strcmp(name + len - 4, ".chk") != 0) {
            continue;
        }
        size_t tag_len = len - 4;
        if (*count_out >= capacity) {
            capacity *= 2;
            char **tmp = realloc(tags, capacity * sizeof(char *));
            if (tmp == NULL) {
                closedir(dir);
                for (size_t i = 0; i < *count_out; ++i) {
                    free(tags[i]);
                }
                free(tags);
                return STORAGE_ERR_IO;
            }
            tags = tmp;
        }
        char *tag = calloc(tag_len + 1, 1);
        if (tag == NULL) {
            closedir(dir);
            for (size_t i = 0; i < *count_out; ++i) {
                free(tags[i]);
            }
            free(tags);
            return STORAGE_ERR_IO;
        }
        memcpy(tag, name, tag_len);
        tags[*count_out] = tag;
        (*count_out)++;
    }
    closedir(dir);
    *tags_out = tags;
    return STORAGE_OK;
}

int storage_engine_view_checkpoint(storage_engine_t *engine, const char *filename, const char *tag, char **content_out) {
    if (engine == NULL || filename == NULL || tag == NULL || content_out == NULL) {
        return STORAGE_ERR_INVALID;
    }
    if (!is_valid_checkpoint_tag(tag)) {
        return STORAGE_ERR_INVALID;
    }
    char checkpoint_path[PATH_MAX];
    if (ss_checkpoint_path(checkpoint_path, sizeof(checkpoint_path), engine->root_path, engine->ss_id, filename, tag) != 0) {
        return STORAGE_ERR_IO;
    }
    size_t len = 0;
    char *content = read_file_if_exists(checkpoint_path, &len);
    if (content == NULL) {
        return STORAGE_ERR_NOT_FOUND;
    }
    *content_out = content;
    return STORAGE_OK;
}

int storage_engine_revert_checkpoint(storage_engine_t *engine, const char *filename, const char *tag) {
    if (engine == NULL || filename == NULL || tag == NULL) {
        return STORAGE_ERR_INVALID;
    }
    char checkpoint_path[PATH_MAX];
    if (ss_checkpoint_path(checkpoint_path, sizeof(checkpoint_path), engine->root_path, engine->ss_id, filename, tag) != 0) {
        return STORAGE_ERR_IO;
    }
    size_t snapshot_len = 0;
    char *snapshot = read_file_if_exists(checkpoint_path, &snapshot_len);
    if (snapshot == NULL) {
        return STORAGE_ERR_NOT_FOUND;
    }

    storage_file_t *file = get_or_load_file(engine, filename, NULL);
    if (file == NULL) {
        free(snapshot);
        return STORAGE_ERR_NOT_FOUND;
    }

    pthread_mutex_lock(&file->file_mutex);
    char *new_content = calloc(1, snapshot_len + 1);
    if (new_content == NULL) {
        pthread_mutex_unlock(&file->file_mutex);
        free(snapshot);
        return STORAGE_ERR_IO;
    }
    memcpy(new_content, snapshot, snapshot_len);
    free(snapshot);

    free(file->undo_snapshot);
    file->undo_snapshot = file->content;
    file->undo_len = file->content_len;

    file->content = new_content;
    file->content_len = snapshot_len;
    file->modified_at = time(NULL);
    file->last_accessed = file->modified_at;
    update_metadata_from_content(file);
    int rc_content = persist_content(engine, file);
    int rc_meta = persist_metadata(engine, file);
    int rc_undo = persist_undo(engine, file);
    pthread_mutex_unlock(&file->file_mutex);
    if (rc_content != 0 || rc_meta != 0 || rc_undo != 0) {
        return STORAGE_ERR_IO;
    }
    return STORAGE_OK;
}

int storage_engine_import(storage_engine_t *engine, const char *filename, const char *content) {
    if (engine == NULL || filename == NULL || content == NULL) {
        return STORAGE_ERR_INVALID;
    }
    /* Use "system" as owner if creating new, otherwise preserve existing owner */
    storage_file_t *file = get_or_load_file(engine, filename, "system");
    if (file == NULL) {
        return STORAGE_ERR_IO;
    }
    
    pthread_mutex_lock(&file->file_mutex);
    
    /* Update content */
    size_t new_len = strlen(content);
    char *new_content = strdup(content);
    if (new_content == NULL) {
        pthread_mutex_unlock(&file->file_mutex);
        return STORAGE_ERR_IO;
    }
    
    free(file->content);
    file->content = new_content;
    file->content_len = new_len;
    
    /* Update metadata */
    time_t now = time(NULL);
    file->modified_at = now;
    /* Set last_accessed if it's currently 0 (new file from replication) */
    if (file->last_accessed == 0) {
        file->last_accessed = now;
    }
    update_metadata_from_content(file);
    
    /* Persist */
    if (persist_content(engine, file) != 0 || persist_metadata(engine, file) != 0) {
        pthread_mutex_unlock(&file->file_mutex);
        return STORAGE_ERR_IO;
    }
    
    pthread_mutex_unlock(&file->file_mutex);
    return STORAGE_OK;
}

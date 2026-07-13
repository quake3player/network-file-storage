#ifndef STORAGE_ENGINE_H
#define STORAGE_ENGINE_H

#include <limits.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct storage_engine storage_engine_t;

typedef struct {
    size_t index;
    const char *text; /* tokens inserted at index, split on spaces */
} storage_word_update_t;

typedef struct {
    char filename[256];
    char owner[128];
    size_t word_count;
    size_t char_count;
    size_t sentence_count;
    time_t created_at;
    time_t modified_at;
    time_t last_accessed;
    char last_accessed_by[128];
} storage_file_info_t;

typedef struct {
    char path[PATH_MAX];
    char owner[128];
    int accessible;
} storage_folder_info_t;

#define STORAGE_OK 0
#define STORAGE_ERR_NOT_FOUND -1
#define STORAGE_ERR_INVALID -2
#define STORAGE_ERR_LOCKED -3
#define STORAGE_ERR_IO -4
#define STORAGE_ERR_EXISTS -5
#define STORAGE_ERR_NOT_FOLDER -6
#define STORAGE_ERR_NO_ACCESS -7

int storage_engine_init(storage_engine_t **engine_out, const char *base_dir, const char *ss_id);
void storage_engine_destroy(storage_engine_t *engine);

int storage_engine_create_file(storage_engine_t *engine, const char *filename, const char *owner);
int storage_engine_delete_file(storage_engine_t *engine, const char *filename);
int storage_engine_create_folder(storage_engine_t *engine, const char *foldername, const char *owner);
int storage_engine_move_file(storage_engine_t *engine, const char *filename, const char *foldername);
int storage_engine_list_folder(storage_engine_t *engine,
                              const char *foldername,
                              const char *username,
                              char ***files_out,
                              size_t *file_count_out,
                              char ***folders_out,
                              size_t *folder_count_out);
int storage_engine_list_folders(storage_engine_t *engine,
                                const char *username,
                                int include_all,
                                storage_folder_info_t **folders_out,
                                size_t *count_out);

int storage_engine_read(storage_engine_t *engine, const char *filename, char **content_out);
int storage_engine_write(storage_engine_t *engine,
                         const char *filename,
                         size_t sentence_index,
                         const storage_word_update_t *updates,
                         size_t update_count,
                         char *undo_token_buf,
                         size_t undo_token_len);
int storage_engine_unlock_sentence(storage_engine_t *engine, const char *filename, size_t sentence_index);
int storage_engine_undo(storage_engine_t *engine, const char *filename);
int storage_engine_info(storage_engine_t *engine, const char *filename, storage_file_info_t *info_out);
int storage_engine_mark_access(storage_engine_t *engine, const char *filename, const char *username);
int storage_engine_checkpoint(storage_engine_t *engine, const char *filename, const char *tag);
int storage_engine_list_checkpoints(storage_engine_t *engine, const char *filename, char ***tags_out, size_t *count_out);
int storage_engine_view_checkpoint(storage_engine_t *engine, const char *filename, const char *tag, char **content_out);
int storage_engine_revert_checkpoint(storage_engine_t *engine, const char *filename, const char *tag);

/* Import raw content (for replication) */
int storage_engine_import(storage_engine_t *engine, const char *filename, const char *content);

#ifdef __cplusplus
}
#endif

#endif

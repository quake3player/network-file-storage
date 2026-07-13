#ifndef FILE_INDEX_H
#define FILE_INDEX_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define MAX_FILENAME_LEN 256
#define MAX_USERNAME_LEN 128
#define MAX_SS_PER_FILE 8

typedef struct {
    char username[MAX_USERNAME_LEN];
    int can_read;
    int can_write;
} acl_entry_t;

typedef struct {
    char filename[MAX_FILENAME_LEN];
    char owner[MAX_USERNAME_LEN];
    char ss_ids[MAX_SS_PER_FILE][128];
    size_t ss_count;
    acl_entry_t *acl;
    size_t acl_count;
    size_t acl_capacity;
    time_t created_at;
    time_t modified_at;
    size_t word_count;
    size_t char_count;
    size_t sentence_count;
} file_record_t;

typedef struct trie_node trie_node_t;

typedef struct {
    trie_node_t *root;
    size_t file_count;
} file_index_t;

typedef struct cache_entry {
    char filename[MAX_FILENAME_LEN];
    file_record_t *record;
    time_t last_access;
    struct cache_entry *prev;
    struct cache_entry *next;
} cache_entry_t;

typedef struct {
    cache_entry_t *head;
    cache_entry_t *tail;
    size_t capacity;
    size_t size;
} lru_cache_t;

int file_index_init(file_index_t **index_out);
void file_index_destroy(file_index_t *index);

int file_index_add(file_index_t *index, const char *filename, const char *owner, const char *ss_id);
file_record_t *file_index_lookup(file_index_t *index, const char *filename);
int file_index_remove(file_index_t *index, const char *filename);
int file_index_add_ss_replica(file_index_t *index, const char *filename, const char *ss_id);
file_record_t **file_index_get_all(file_index_t *index, size_t *count_out);

int file_record_add_acl(file_record_t *record, const char *username, int can_read, int can_write);
int file_record_remove_acl(file_record_t *record, const char *username);
acl_entry_t *file_record_find_acl(file_record_t *record, const char *username);
int file_record_check_access(file_record_t *record, const char *username, int need_write);

int lru_cache_init(lru_cache_t **cache_out, size_t capacity);
void lru_cache_destroy(lru_cache_t *cache);
file_record_t *lru_cache_get(lru_cache_t *cache, file_index_t *index, const char *filename);
void lru_cache_invalidate(lru_cache_t *cache, const char *filename);

#endif // FILE_INDEX_H

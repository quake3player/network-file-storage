#define _POSIX_C_SOURCE 200809L

#include "file_index.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TRIE_ALPHABET_SIZE 256

struct trie_node {
    struct trie_node *children[TRIE_ALPHABET_SIZE];
    file_record_t *record;
};

static trie_node_t *trie_node_create(void) {
    trie_node_t *node = calloc(1, sizeof(*node));
    return node;
}

static void trie_node_destroy(trie_node_t *node) {
    if (node == NULL) {
        return;
    }
    for (size_t i = 0; i < TRIE_ALPHABET_SIZE; ++i) {
        if (node->children[i] != NULL) {
            trie_node_destroy(node->children[i]);
        }
    }
    if (node->record != NULL) {
        free(node->record->acl);
        free(node->record);
    }
    free(node);
}

int file_index_init(file_index_t **index_out) {
    if (index_out == NULL) {
        errno = EINVAL;
        return -1;
    }
    file_index_t *index = calloc(1, sizeof(*index));
    if (index == NULL) {
        return -1;
    }
    index->root = trie_node_create();
    if (index->root == NULL) {
        free(index);
        return -1;
    }
    *index_out = index;
    return 0;
}

void file_index_destroy(file_index_t *index) {
    if (index == NULL) {
        return;
    }
    trie_node_destroy(index->root);
    free(index);
}

int file_index_add(file_index_t *index, const char *filename, const char *owner, const char *ss_id) {
    if (index == NULL || filename == NULL || owner == NULL || ss_id == NULL) {
        errno = EINVAL;
        return -1;
    }
    trie_node_t *node = index->root;
    for (const char *p = filename; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (node->children[c] == NULL) {
            node->children[c] = trie_node_create();
            if (node->children[c] == NULL) {
                return -1;
            }
        }
        node = node->children[c];
    }
    
    if (node->record == NULL) {
        node->record = calloc(1, sizeof(file_record_t));
        if (node->record == NULL) {
            return -1;
        }
        strncpy(node->record->filename, filename, sizeof(node->record->filename) - 1);
        strncpy(node->record->owner, owner, sizeof(node->record->owner) - 1);
        node->record->created_at = time(NULL);
        node->record->modified_at = node->record->created_at;
        index->file_count++;
    }
    
    if (node->record->ss_count < MAX_SS_PER_FILE) {
        int already_exists = 0;
        for (size_t i = 0; i < node->record->ss_count; ++i) {
            if (strcmp(node->record->ss_ids[i], ss_id) == 0) {
                already_exists = 1;
                break;
            }
        }
        if (!already_exists) {
            strncpy(node->record->ss_ids[node->record->ss_count], ss_id, 127);
            node->record->ss_count++;
        }
    }
    
    return 0;
}

file_record_t *file_index_lookup(file_index_t *index, const char *filename) {
    if (index == NULL || filename == NULL) {
        return NULL;
    }
    trie_node_t *node = index->root;
    for (const char *p = filename; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (node->children[c] == NULL) {
            return NULL;
        }
        node = node->children[c];
    }
    return node->record;
}

int file_index_remove(file_index_t *index, const char *filename) {
    if (index == NULL || filename == NULL) {
        errno = EINVAL;
        return -1;
    }
    trie_node_t *node = index->root;
    for (const char *p = filename; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (node->children[c] == NULL) {
            errno = ENOENT;
            return -1;
        }
        node = node->children[c];
    }
    if (node->record != NULL) {
        free(node->record->acl);
        free(node->record);
        node->record = NULL;
        index->file_count--;
        return 0;
    }
    errno = ENOENT;
    return -1;
}

int file_index_add_ss_replica(file_index_t *index, const char *filename, const char *ss_id) {
    file_record_t *record = file_index_lookup(index, filename);
    if (record == NULL) {
        errno = ENOENT;
        return -1;
    }
    if (record->ss_count >= MAX_SS_PER_FILE) {
        errno = ENOSPC;
        return -1;
    }
    for (size_t i = 0; i < record->ss_count; ++i) {
        if (strcmp(record->ss_ids[i], ss_id) == 0) {
            return 0;
        }
    }
    strncpy(record->ss_ids[record->ss_count], ss_id, 127);
    record->ss_count++;
    return 0;
}

int file_record_add_acl(file_record_t *record, const char *username, int can_read, int can_write) {
    if (record == NULL || username == NULL) {
        errno = EINVAL;
        return -1;
    }
    for (size_t i = 0; i < record->acl_count; ++i) {
        if (strcmp(record->acl[i].username, username) == 0) {
            record->acl[i].can_read = can_read;
            record->acl[i].can_write = can_write;
            return 0;
        }
    }
    if (record->acl_count >= record->acl_capacity) {
        size_t new_cap = record->acl_capacity ? record->acl_capacity * 2 : 4;
        acl_entry_t *new_acl = realloc(record->acl, new_cap * sizeof(acl_entry_t));
        if (new_acl == NULL) {
            return -1;
        }
        record->acl = new_acl;
        record->acl_capacity = new_cap;
    }
    strncpy(record->acl[record->acl_count].username, username, sizeof(record->acl[0].username) - 1);
    record->acl[record->acl_count].can_read = can_read;
    record->acl[record->acl_count].can_write = can_write;
    record->acl_count++;
    return 0;
}

int file_record_remove_acl(file_record_t *record, const char *username) {
    if (record == NULL || username == NULL) {
        errno = EINVAL;
        return -1;
    }
    for (size_t i = 0; i < record->acl_count; ++i) {
        if (strcmp(record->acl[i].username, username) == 0) {
            memmove(&record->acl[i], &record->acl[i + 1],
                    (record->acl_count - i - 1) * sizeof(acl_entry_t));
            record->acl_count--;
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

acl_entry_t *file_record_find_acl(file_record_t *record, const char *username) {
    if (record == NULL || username == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < record->acl_count; ++i) {
        if (strcmp(record->acl[i].username, username) == 0) {
            return &record->acl[i];
        }
    }
    return NULL;
}

int file_record_check_access(file_record_t *record, const char *username, int need_write) {
    if (record == NULL || username == NULL) {
        return 0;
    }
    if (strcmp(record->owner, username) == 0) {
        return 1;
    }
    acl_entry_t *entry = file_record_find_acl(record, username);
    if (entry == NULL) {
        return 0;
    }
    if (need_write) {
        return entry->can_write;
    }
    return entry->can_read || entry->can_write;
}

int lru_cache_init(lru_cache_t **cache_out, size_t capacity) {
    if (cache_out == NULL || capacity == 0) {
        errno = EINVAL;
        return -1;
    }
    lru_cache_t *cache = calloc(1, sizeof(*cache));
    if (cache == NULL) {
        return -1;
    }
    cache->capacity = capacity;
    *cache_out = cache;
    return 0;
}

void lru_cache_destroy(lru_cache_t *cache) {
    if (cache == NULL) {
        return;
    }
    cache_entry_t *entry = cache->head;
    while (entry != NULL) {
        cache_entry_t *next = entry->next;
        free(entry);
        entry = next;
    }
    free(cache);
}

static void lru_cache_remove_entry(lru_cache_t *cache, cache_entry_t *entry) {
    if (entry->prev != NULL) {
        entry->prev->next = entry->next;
    } else {
        cache->head = entry->next;
    }
    if (entry->next != NULL) {
        entry->next->prev = entry->prev;
    } else {
        cache->tail = entry->prev;
    }
    cache->size--;
}

static void lru_cache_add_to_front(lru_cache_t *cache, cache_entry_t *entry) {
    entry->prev = NULL;
    entry->next = cache->head;
    if (cache->head != NULL) {
        cache->head->prev = entry;
    }
    cache->head = entry;
    if (cache->tail == NULL) {
        cache->tail = entry;
    }
    cache->size++;
}

file_record_t *lru_cache_get(lru_cache_t *cache, file_index_t *index, const char *filename) {
    if (cache == NULL || index == NULL || filename == NULL) {
        return NULL;
    }
    
    cache_entry_t *entry = cache->head;
    while (entry != NULL) {
        if (strcmp(entry->filename, filename) == 0) {
            lru_cache_remove_entry(cache, entry);
            lru_cache_add_to_front(cache, entry);
            entry->last_access = time(NULL);
            return entry->record;
        }
        entry = entry->next;
    }
    
    file_record_t *record = file_index_lookup(index, filename);
    if (record == NULL) {
        return NULL;
    }
    
    cache_entry_t *new_entry = calloc(1, sizeof(*new_entry));
    if (new_entry == NULL) {
        return record;
    }
    strncpy(new_entry->filename, filename, sizeof(new_entry->filename) - 1);
    new_entry->record = record;
    new_entry->last_access = time(NULL);
    
    if (cache->size >= cache->capacity && cache->tail != NULL) {
        cache_entry_t *evict = cache->tail;
        lru_cache_remove_entry(cache, evict);
        free(evict);
    }
    
    lru_cache_add_to_front(cache, new_entry);
    return record;
}

void lru_cache_invalidate(lru_cache_t *cache, const char *filename) {
    if (cache == NULL || filename == NULL) {
        return;
    }
    cache_entry_t *entry = cache->head;
    while (entry != NULL) {
        cache_entry_t *next = entry->next;
        if (strcmp(entry->filename, filename) == 0) {
            lru_cache_remove_entry(cache, entry);
            free(entry);
            return;
        }
        entry = next;
    }
}

/* Helper function to recursively collect all file records from trie */
static void collect_files_recursive(trie_node_t *node, file_record_t ***records, size_t *count, size_t *capacity) {
    if (node == NULL) {
        return;
    }
    
    if (node->record != NULL) {
        /* Grow array if needed */
        if (*count >= *capacity) {
            size_t new_capacity = *capacity == 0 ? 16 : *capacity * 2;
            file_record_t **new_records = realloc(*records, new_capacity * sizeof(file_record_t *));
            if (new_records != NULL) {
                *records = new_records;
                *capacity = new_capacity;
            } else {
                return; /* Out of memory, skip this record */
            }
        }
        (*records)[*count] = node->record;
        (*count)++;
    }
    
    for (size_t i = 0; i < TRIE_ALPHABET_SIZE; i++) {
        if (node->children[i] != NULL) {
            collect_files_recursive(node->children[i], records, count, capacity);
        }
    }
}

/* Get all file records in the index */
file_record_t **file_index_get_all(file_index_t *index, size_t *count_out) {
    if (index == NULL || count_out == NULL) {
        return NULL;
    }
    
    file_record_t **records = NULL;
    size_t count = 0;
    size_t capacity = 0;
    
    collect_files_recursive(index->root, &records, &count, &capacity);
    
    *count_out = count;
    return records;
}

#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include <stddef.h>
#include <time.h>

int ns_registry_path(char *buf, size_t len, const char *base_dir);
int ns_files_index_path(char *buf, size_t len, const char *base_dir);
int ns_acl_path(char *buf, size_t len, const char *base_dir, const char *filename);
int ns_cache_path(char *buf, size_t len, const char *base_dir);
int nm_log_path(char *buf, size_t len, const char *base_dir, time_t when);
int ns_requests_path(char *buf, size_t len, const char *base_dir, const char *filename);

int ss_root_path(char *buf, size_t len, const char *base_dir, const char *ss_id);
int ss_file_path(char *buf, size_t len, const char *base_dir, const char *ss_id, const char *filename);
int ss_metadata_path(char *buf, size_t len, const char *base_dir, const char *ss_id, const char *filename);
int ss_undo_path(char *buf, size_t len, const char *base_dir, const char *ss_id, const char *filename);
int ss_log_path(char *buf, size_t len, const char *base_dir, const char *ss_id, time_t when);
int ss_checkpoint_dir(char *buf, size_t len, const char *base_dir, const char *ss_id, const char *filename);
int ss_checkpoint_path(char *buf, size_t len, const char *base_dir, const char *ss_id, const char *filename, const char *tag);

#endif

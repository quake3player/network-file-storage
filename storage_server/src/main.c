#define _XOPEN_SOURCE 600
#define _POSIX_C_SOURCE 200809L

#include "net.h"
#include "protocol.h"
#include "storage_engine.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Structure to track locked sentences */
typedef struct sentence_lock {
    char filename[256];
    unsigned long sentence_idx;
    time_t file_version; /* file modified_at captured when lock was acquired */
    struct sentence_lock *next;
} sentence_lock_t;

typedef struct {
    char ss_id[128];
    char nm_host[256];
    uint16_t nm_port;
    uint16_t client_port;
    int nm_fd;
    int client_listen_fd;
    storage_engine_t *engine;
    volatile int running;
    pthread_mutex_t lock;
    unsigned int active_readers;
    unsigned int active_writers;
    uint32_t request_counter;
    sentence_lock_t *locked_sentences;  /* Linked list of locked sentences */
} ss_state_t;

static ss_state_t g_state;

/* Logging helper */
static void ss_log(const char *fmt, ...) {
    time_t now = time(NULL);
    struct tm tm_snapshot;
    localtime_r(&now, &tm_snapshot);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_snapshot);

    /* ANSI colors */
    const char *COLOR_BLUE = "\033[34m";
    const char *COLOR_RESET = "\033[0m";
    const char *COLOR_YELLOW = "\033[33m";
    const char *COLOR_RED = "\033[31m";
    const char *COLOR_GREEN = "\033[32m";

    const char *color = COLOR_RESET;
    if (strstr(fmt, "Error") || strstr(fmt, "Failed") || strstr(fmt, "Shutting down")) {
        color = COLOR_RED;
    } else if (strstr(fmt, "Warning") || strstr(fmt, "Replicating")) {
        color = COLOR_YELLOW;
    } else if (strstr(fmt, "Registered") || strstr(fmt, "initialized") || strstr(fmt, "listening")) {
        color = COLOR_GREEN;
    }

    va_list args;
    va_start(args, fmt);
    fprintf(stdout, "%s[%s]%s ", COLOR_BLUE, timestamp, COLOR_RESET);
    fprintf(stdout, "%s", color);
    vfprintf(stdout, fmt, args);
    fprintf(stdout, "%s\n", COLOR_RESET);
    fflush(stdout);
    va_end(args);
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <nm-host> <nm-port> <ss-id> <client-port>\n", prog);
}

static void signal_handler(int sig) {
    (void)sig;
    g_state.running = 0;
}

static int send_message(int fd, protocol_opcode_t opcode, uint32_t request_id, const char *payload) {
    message_header_t header = {
        .version = PROTOCOL_VERSION,
        .opcode = opcode,
        .request_id = request_id,
        .payload_len = payload != NULL ? (uint32_t)strlen(payload) : 0u
    };
    uint8_t header_buf[PROTOCOL_HEADER_SIZE];
    if (protocol_encode_header(&header, header_buf) != 0) {
        return -1;
    }
    if (send_all(fd, header_buf, sizeof(header_buf)) < 0) {
        return -1;
    }
    if (payload != NULL && header.payload_len > 0) {
        if (send_all(fd, payload, header.payload_len) < 0) {
            return -1;
        }
    }
    return 0;
}

static int recv_message(int fd, message_header_t *header, char **payload_out) {
    uint8_t header_buf[PROTOCOL_HEADER_SIZE];
    int rc = recv_all(fd, header_buf, sizeof(header_buf));
    if (rc <= 0) {
        return -1;
    }
    if (protocol_decode_header(header_buf, header) != 0) {
        return -1;
    }
    char *payload = NULL;
    if (header->payload_len > 0) {
        payload = calloc(1, header->payload_len + 1);
        if (payload == NULL) {
            return -1;
        }
        rc = recv_all(fd, payload, header->payload_len);
        if (rc <= 0) {
            free(payload);
            return -1;
        }
        payload[header->payload_len] = '\0';
    }
    *payload_out = payload;
    return 0;
}

/* Simple JSON helpers */
static const char *json_get_string(const char *json, const char *key, char *buf, size_t bufsize) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char *p = strstr(json, search);
    if (p == NULL) {
        return NULL;
    }
    p += strlen(search);
    const char *end = strchr(p, '"');
    if (end == NULL) {
        return NULL;
    }
    size_t len = (size_t)(end - p);
    if (len >= bufsize) {
        len = bufsize - 1;
    }
    memcpy(buf, p, len);
    buf[len] = '\0';
    return buf;
}

static void send_error_with_code(int client_fd, const char *message, int code) {
    char error[256];
    snprintf(error, sizeof(error), "{\"error\":\"%s\",\"code\":%d}", message != NULL ? message : "Operation failed", code);
    send_message(client_fd, OP_ERROR, 0, error);
}

static unsigned long json_get_ulong(const char *json, const char *key) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (p == NULL) {
        return 0;
    }
    p += strlen(search);
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return strtoul(p, NULL, 10);
}

/* Client request handlers */
static void handle_read_request(int client_fd, const char *payload) {
    char filename[256] = {0};
    json_get_string(payload, "filename", filename, sizeof(filename));
    char username[128] = "unknown";
    json_get_string(payload, "username", username, sizeof(username));
    
    pthread_mutex_lock(&g_state.lock);
    g_state.active_readers++;
    pthread_mutex_unlock(&g_state.lock);
    
    char *content = NULL;
    int rc = storage_engine_read(g_state.engine, filename, &content);
    
    pthread_mutex_lock(&g_state.lock);
    g_state.active_readers--;
    pthread_mutex_unlock(&g_state.lock);
    
    if (rc == STORAGE_OK && content != NULL) {
        /* Calculate required buffer size: JSON overhead + escaped content */
        size_t content_len = strlen(content);
        size_t max_response_len = 100 + (content_len * 2);  /* Worst case: every char escaped */
        char *response = malloc(max_response_len);
        
        if (response == NULL) {
            char error[256];
            snprintf(error, sizeof(error), "{\"error\":\"Memory allocation failed\",\"code\":%d}", STORAGE_ERR_INVALID);
            send_message(client_fd, OP_ERROR, 0, error);
            free(content);
            return;
        }
        
        snprintf(response, max_response_len, "{\"content\":\"%s\"}", content);
        send_message(client_fd, OP_DATA_RESPONSE, 0, response);
        storage_engine_mark_access(g_state.engine, filename, username);
        free(response);
        free(content);
    } else {
        char error[256];
        snprintf(error, sizeof(error), "{\"error\":\"File not found or read error\",\"code\":%d}", rc);
        send_message(client_fd, OP_ERROR, 0, error);
    }
}

/* Helper function to check if a sentence is already locked */
static int is_sentence_locked(const char *filename, unsigned long sentence_idx) {
    sentence_lock_t *curr = g_state.locked_sentences;
    while (curr != NULL) {
        if (strcmp(curr->filename, filename) == 0 && curr->sentence_idx == sentence_idx) {
            return 1;  /* Already locked */
        }
        curr = curr->next;
    }
    return 0;  /* Not locked */
}

/* Helper function to add a sentence lock */
static int add_sentence_lock(const char *filename, unsigned long sentence_idx, time_t file_version) {
    sentence_lock_t *new_lock = malloc(sizeof(sentence_lock_t));
    if (new_lock == NULL) {
        return -1;
    }
    strncpy(new_lock->filename, filename, sizeof(new_lock->filename) - 1);
    new_lock->filename[sizeof(new_lock->filename) - 1] = '\0';
    new_lock->sentence_idx = sentence_idx;
    new_lock->file_version = file_version;
    new_lock->next = g_state.locked_sentences;
    g_state.locked_sentences = new_lock;
    return 0;
}

/* Helper function to remove a sentence lock */
static void remove_sentence_lock(const char *filename, unsigned long sentence_idx) {
    sentence_lock_t **curr_ptr = &g_state.locked_sentences;
    while (*curr_ptr != NULL) {
        sentence_lock_t *curr = *curr_ptr;
        if (strcmp(curr->filename, filename) == 0 && curr->sentence_idx == sentence_idx) {
            *curr_ptr = curr->next;
            free(curr);
            return;
        }
        curr_ptr = &curr->next;
    }
}

/* Return stored file_version for a locked sentence. Returns 0 on success, -1 if not found */
static int get_locked_sentence_version(const char *filename, unsigned long sentence_idx, time_t *out_version) {
    if (filename == NULL || out_version == NULL) return -1;
    int found = -1;
    pthread_mutex_lock(&g_state.lock);
    sentence_lock_t *curr = g_state.locked_sentences;
    while (curr != NULL) {
        if (strcmp(curr->filename, filename) == 0 && curr->sentence_idx == sentence_idx) {
            *out_version = curr->file_version;
            found = 0;
            break;
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&g_state.lock);
    return found;
}

static void handle_write_request(int client_fd, const char *payload) {
    char filename[256] = {0};
    json_get_string(payload, "filename", filename, sizeof(filename));
    unsigned long sentence_idx = json_get_ulong(payload, "sentence");
    char username[128] = "unknown";
    json_get_string(payload, "username", username, sizeof(username));
    
    /* Validate sentence index before granting lock */
    storage_file_info_t info;
    int rc = storage_engine_info(g_state.engine, filename, &info);
    
    if (rc != STORAGE_OK) {
        char error[256];
        snprintf(error, sizeof(error), "{\"error\":\"File not found\",\"code\":%d}", rc);
        send_message(client_fd, OP_ERROR, 0, error);
        return;
    }
    
    /* Check if sentence index is valid (0-indexed, can be at most sentence_count) */
    if (sentence_idx > info.sentence_count) {
        char error[256];
        snprintf(error, sizeof(error), 
                "{\"error\":\"Sentence index out of range\",\"code\":%d}", STORAGE_ERR_INVALID);
        send_message(client_fd, OP_ERROR, 0, error);
        return;
    }
    
    /* Check if this specific sentence is already locked */
    pthread_mutex_lock(&g_state.lock);
    if (is_sentence_locked(filename, sentence_idx)) {
        pthread_mutex_unlock(&g_state.lock);
        char error[256];
        snprintf(error, sizeof(error), 
                "{\"error\":\"Sentence is already locked by another client\",\"code\":%d}", 
                STORAGE_ERR_INVALID);
        send_message(client_fd, OP_ERROR, 0, error);
        return;
    }
    
    /* Add this sentence to the locked list (record file version) */
    if (add_sentence_lock(filename, sentence_idx, info.modified_at) != 0) {
        pthread_mutex_unlock(&g_state.lock);
        char error[256];
        snprintf(error, sizeof(error), "{\"error\":\"Failed to acquire lock\",\"code\":%d}", 
                STORAGE_ERR_INVALID);
        send_message(client_fd, OP_ERROR, 0, error);
        return;
    }
    
    g_state.active_writers++;
    pthread_mutex_unlock(&g_state.lock);
    
    /* Read file content to get sentence text and word count */
    char *content = NULL;
    rc = storage_engine_read(g_state.engine, filename, &content);
    
    char sentence_content[512] = "";
    size_t word_count = 0;
    
    if (rc == STORAGE_OK && content != NULL && strlen(content) > 0) {
        /* Parse sentences to find the target one */
        size_t current_sentence = 0;
        char *pos = content;
        char *sentence_start = pos;
        int found_delimiter = 0;
        
        while (*pos) {
            if (*pos == '.' || *pos == '!' || *pos == '?') {
                if (current_sentence == sentence_idx) {
                    size_t len = pos - sentence_start + 1;
                    if (len > sizeof(sentence_content) - 1) {
                        len = sizeof(sentence_content) - 1;
                    }
                    strncpy(sentence_content, sentence_start, len);
                    sentence_content[len] = '\0';
                    found_delimiter = 1;
                    break;
                }
                current_sentence++;
                pos++;
                while (*pos == ' ' || *pos == '\t' || *pos == '\n') pos++;
                sentence_start = pos;
            } else {
                pos++;
            }
        }
        
        /* If we didn't find a delimiter and we're looking for sentence at current_sentence position */
        /* Allow editing incomplete text (text without delimiter) as a draft */
        if (!found_delimiter && current_sentence == sentence_idx && sentence_start && *sentence_start != '\0') {
            size_t len = strlen(sentence_start);
            if (len > sizeof(sentence_content) - 1) {
                len = sizeof(sentence_content) - 1;
            }
            strncpy(sentence_content, sentence_start, len);
            sentence_content[len] = '\0';
        }
        
        /* Count words in the sentence content we found */
        if (sentence_content[0] != '\0') {
            /* Count words in sentence - make a copy since strtok is destructive */
            char temp_content[512];
            strncpy(temp_content, sentence_content, sizeof(temp_content) - 1);
            temp_content[sizeof(temp_content) - 1] = '\0';
            
            char *word = strtok(temp_content, " \t\n");
            while (word != NULL) {
                word_count++;
                word = strtok(NULL, " \t\n");
            }
        }
        
        free(content);
    }
    
    /* Acknowledge lock acquired with sentence info */
    char ack[512];
    snprintf(ack, sizeof(ack), "{\"status\":\"locked\",\"sentence\":%lu,\"word_count\":%zu}", 
             sentence_idx, word_count);
    send_message(client_fd, OP_WRITE_ACK, 0, ack);
    
    /* Collect word updates until ETIRW */
    storage_word_update_t updates[256];
    size_t update_count = 0;
    int committed = 0;
    
    while (1) {
        message_header_t hdr;
        char *update_payload = NULL;
        if (recv_message(client_fd, &hdr, &update_payload) != 0) {
            break;
        }
        
        if (hdr.opcode == OP_WRITE_COMMIT) {
            /* Before applying updates, ensure file hasn't been modified since lock was granted */
            time_t locked_version = 0;
            if (get_locked_sentence_version(filename, sentence_idx, &locked_version) == 0) {
                storage_file_info_t cur_info;
                if (storage_engine_info(g_state.engine, filename, &cur_info) == STORAGE_OK) {
                    if (cur_info.modified_at > locked_version) {
                        char error[256];
                        snprintf(error, sizeof(error), "{\"error\":\"File modified during edit\",\"code\":%d}", STORAGE_ERR_INVALID);
                        send_message(client_fd, OP_ERROR, 0, error);
                        free(update_payload);
                        break;
                    }
                }
            }

            /* Apply all updates */
            char undo_token[64] = {0};
            int write_rc = storage_engine_write(g_state.engine, filename, 
                                               sentence_idx, updates, update_count,
                                               undo_token, sizeof(undo_token));

            if (write_rc == STORAGE_OK) {
                char success[256];
                snprintf(success, sizeof(success), 
                        "{\"status\":\"committed\",\"undo_token\":\"%s\"}", undo_token);
                send_message(client_fd, OP_WRITE_ACK, 0, success);
                storage_engine_mark_access(g_state.engine, filename, username);
                committed = 1;
            } else {
                char error[256];
                snprintf(error, sizeof(error), "{\"error\":\"Write failed\",\"code\":%d}", write_rc);
                send_message(client_fd, OP_ERROR, 0, error);
            }
            free(update_payload);
            break;
        } else if (hdr.opcode == OP_WRITE_UPDATE) {
            /* Parse word update: {"word_index":N,"content":"text"} */
            unsigned long word_idx = json_get_ulong(update_payload, "word_index");
            char content[512] = {0};
            json_get_string(update_payload, "content", content, sizeof(content));
            
            if (update_count < 256) {
                updates[update_count].index = word_idx;
                updates[update_count].text = strdup(content);
                update_count++;
            }
            free(update_payload);
        } else {
            free(update_payload);
            break;
        }
    }
    
    /* Release lock */
    storage_engine_unlock_sentence(g_state.engine, filename, sentence_idx);
    
    /* Remove sentence lock from our tracking list */
    pthread_mutex_lock(&g_state.lock);
    remove_sentence_lock(filename, sentence_idx);
    g_state.active_writers--;
    pthread_mutex_unlock(&g_state.lock);
    
    /* Cleanup */
    for (size_t i = 0; i < update_count; i++) {
        free((void *)updates[i].text);
    }
    
    (void)committed;
}

static void handle_stream_request(int client_fd, const char *payload) {
    char filename[256] = {0};
    json_get_string(payload, "filename", filename, sizeof(filename));
    char username[128] = "unknown";
    json_get_string(payload, "username", username, sizeof(username));
    
    pthread_mutex_lock(&g_state.lock);
    g_state.active_readers++;
    pthread_mutex_unlock(&g_state.lock);
    
    char *content = NULL;
    int rc = storage_engine_read(g_state.engine, filename, &content);
    
    if (rc == STORAGE_OK && content != NULL) {
        /* Send word by word with 100ms delay */
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000}; /* 0.1 second */
        char *token = strtok(content, " \t\n");
        while (token != NULL) {
            char chunk[1024];
            snprintf(chunk, sizeof(chunk), "{\"word\":\"%s\"}", token);
            send_message(client_fd, OP_DATA_CHUNK, 0, chunk);
            nanosleep(&delay, NULL);
            token = strtok(NULL, " \t\n");
        }
        /* Send completion marker */
        send_message(client_fd, OP_DATA_RESPONSE, 0, "{\"status\":\"complete\"}");
    storage_engine_mark_access(g_state.engine, filename, username);
        free(content);
    } else {
        char error[256];
        snprintf(error, sizeof(error), "{\"error\":\"File not found\",\"code\":%d}", rc);
        send_message(client_fd, OP_ERROR, 0, error);
    }
    
    pthread_mutex_lock(&g_state.lock);
    g_state.active_readers--;
    pthread_mutex_unlock(&g_state.lock);
}

static void handle_delete_request(int client_fd, const char *payload) {
    char filename[256] = {0};
    json_get_string(payload, "filename", filename, sizeof(filename));
    
    int rc = storage_engine_delete_file(g_state.engine, filename);
    
    if (rc == STORAGE_OK) {
        char response[256];
        snprintf(response, sizeof(response), "{\"status\":\"deleted\"}");
        send_message(client_fd, OP_DATA_RESPONSE, 0, response);
    } else {
        char error[256];
        snprintf(error, sizeof(error), "{\"error\":\"Delete failed\",\"code\":%d}", rc);
        send_message(client_fd, OP_ERROR, 0, error);
    }
}

static void handle_checkpoint_request(int client_fd, const char *payload) {
    char filename[256] = {0};
    char tag[128] = {0};
    char username[128] = {0};
    json_get_string(payload, "filename", filename, sizeof(filename));
    json_get_string(payload, "tag", tag, sizeof(tag));
    json_get_string(payload, "username", username, sizeof(username));

    int rc = storage_engine_checkpoint(g_state.engine, filename, tag);
    if (rc == STORAGE_OK) {
        char response[256];
        snprintf(response, sizeof(response), "{\"status\":\"checkpointed\",\"tag\":\"%s\"}", tag);
        send_message(client_fd, OP_COMMAND_STATUS, 0, response);
        if (username[0] != '\0') {
            storage_engine_mark_access(g_state.engine, filename, username);
        }
    } else if (rc == STORAGE_ERR_EXISTS) {
        send_error_with_code(client_fd, "Checkpoint already exists", ERR_CHECKPOINT_EXISTS);
    } else if (rc == STORAGE_ERR_NOT_FOUND) {
        send_error_with_code(client_fd, "File not found", ERR_FILE_NOT_FOUND);
    } else if (rc == STORAGE_ERR_INVALID) {
        send_error_with_code(client_fd, "Invalid checkpoint tag", ERR_INVALID_SENTENCE);
    } else {
        send_error_with_code(client_fd, "Failed to create checkpoint", ERR_EXECUTION_FAIL);
    }
}

static void handle_listcheckpoints_request(int client_fd, const char *payload) {
    char filename[256] = {0};
    json_get_string(payload, "filename", filename, sizeof(filename));

    char **tags = NULL;
    size_t count = 0;
    int rc = storage_engine_list_checkpoints(g_state.engine, filename, &tags, &count);
    if (rc == STORAGE_OK) {
        char response[8192] = {0};
        size_t offset = snprintf(response, sizeof(response), "{\"checkpoints\":[");
        for (size_t i = 0; i < count && offset < sizeof(response) - 100; ++i) {
            if (i > 0) {
                offset += snprintf(response + offset, sizeof(response) - offset, ",");
            }
            offset += snprintf(response + offset, sizeof(response) - offset, "\"%s\"", tags[i]);
            free(tags[i]);
        }
        free(tags);
        snprintf(response + offset, sizeof(response) - offset, "]}");
        send_message(client_fd, OP_LISTCHECKPOINTS_RESPONSE, 0, response);
    } else if (rc == STORAGE_ERR_NOT_FOUND) {
        send_error_with_code(client_fd, "File not found", ERR_FILE_NOT_FOUND);
    } else {
        send_error_with_code(client_fd, "Failed to list checkpoints", ERR_EXECUTION_FAIL);
    }
}

static void handle_viewcheckpoint_request(int client_fd, const char *payload) {
    char filename[256] = {0};
    char tag[128] = {0};
    json_get_string(payload, "filename", filename, sizeof(filename));
    json_get_string(payload, "tag", tag, sizeof(tag));

    char *content = NULL;
    int rc = storage_engine_view_checkpoint(g_state.engine, filename, tag, &content);
    if (rc == STORAGE_OK && content != NULL) {
        size_t len = strlen(content);
        size_t max_len = len * 2 + 64;
        char *response = malloc(max_len);
        if (response == NULL) {
            free(content);
            send_error_with_code(client_fd, "Out of memory", ERR_EXECUTION_FAIL);
            return;
        }
        snprintf(response, max_len, "{\"content\":\"%s\"}", content);
        send_message(client_fd, OP_DATA_RESPONSE, 0, response);
        free(response);
        free(content);
    } else if (rc == STORAGE_ERR_NOT_FOUND) {
        send_error_with_code(client_fd, "Checkpoint not found", ERR_CHECKPOINT_NOT_FOUND);
    } else {
        send_error_with_code(client_fd, "Failed to read checkpoint", ERR_EXECUTION_FAIL);
    }
}

static void handle_revertcheckpoint_request(int client_fd, const char *payload) {
    char filename[256] = {0};
    char tag[128] = {0};
    char username[128] = {0};
    json_get_string(payload, "filename", filename, sizeof(filename));
    json_get_string(payload, "tag", tag, sizeof(tag));
    json_get_string(payload, "username", username, sizeof(username));

    pthread_mutex_lock(&g_state.lock);
    g_state.active_writers++;
    pthread_mutex_unlock(&g_state.lock);

    int rc = storage_engine_revert_checkpoint(g_state.engine, filename, tag);

    pthread_mutex_lock(&g_state.lock);
    g_state.active_writers--;
    pthread_mutex_unlock(&g_state.lock);

    if (rc == STORAGE_OK) {
        char response[256];
        snprintf(response, sizeof(response), "{\"status\":\"reverted\",\"tag\":\"%s\"}", tag);
        send_message(client_fd, OP_COMMAND_STATUS, 0, response);
        if (username[0] != '\0') {
            storage_engine_mark_access(g_state.engine, filename, username);
        }
    } else if (rc == STORAGE_ERR_NOT_FOUND) {
        send_error_with_code(client_fd, "File or checkpoint not found", ERR_CHECKPOINT_NOT_FOUND);
    } else {
        send_error_with_code(client_fd, "Failed to revert checkpoint", ERR_EXECUTION_FAIL);
    }
}

static void handle_undo_request(int client_fd, const char *payload) {
    char filename[256] = {0};
    json_get_string(payload, "filename", filename, sizeof(filename));
    char username[128] = "unknown";
    json_get_string(payload, "username", username, sizeof(username));
    
    pthread_mutex_lock(&g_state.lock);
    g_state.active_writers++;
    pthread_mutex_unlock(&g_state.lock);
    
    int rc = storage_engine_undo(g_state.engine, filename);
    
    pthread_mutex_lock(&g_state.lock);
    g_state.active_writers--;
    pthread_mutex_unlock(&g_state.lock);
    
    if (rc == STORAGE_OK) {
        char response[256];
        snprintf(response, sizeof(response), "{\"status\":\"OK\",\"message\":\"Undo successful\"}");
        send_message(client_fd, OP_DATA_RESPONSE, 0, response);
        storage_engine_mark_access(g_state.engine, filename, username);
    } else if (rc == STORAGE_ERR_INVALID) {
        char error[256];
        snprintf(error, sizeof(error), "{\"error\":\"No undo history available\",\"code\":%d}", ERR_UNDO_EMPTY);
        send_message(client_fd, OP_ERROR, 0, error);
    } else {
        char error[256];
        snprintf(error, sizeof(error), "{\"error\":\"Undo failed\",\"code\":%d}", rc);
        send_message(client_fd, OP_ERROR, 0, error);
    }
}

static void handle_info_request(int client_fd, const char *payload) {
    char filename[256] = {0};
    json_get_string(payload, "filename", filename, sizeof(filename));
    
    storage_file_info_t info;
    int rc = storage_engine_info(g_state.engine, filename, &info);
    
    if (rc == STORAGE_OK) {
        char response[1024];
        snprintf(response, sizeof(response),
                "{\"filename\":\"%s\",\"owner\":\"%s\",\"word_count\":%zu,"
                "\"char_count\":%zu,\"sentence_count\":%zu,\"created_at\":%ld,"
                "\"modified_at\":%ld,\"last_accessed\":%ld,\"last_accessed_by\":\"%s\"}",
                info.filename, info.owner, info.word_count, info.char_count,
                info.sentence_count, (long)info.created_at, (long)info.modified_at,
                (long)info.last_accessed, info.last_accessed_by);
        send_message(client_fd, OP_DATA_RESPONSE, 0, response);
    } else {
        char error[256];
        snprintf(error, sizeof(error), "{\"error\":\"File not found\",\"code\":%d}", rc);
        send_message(client_fd, OP_ERROR, 0, error);
    }
}

static void handle_create_request(int client_fd, const char *payload) {
    char filename[256] = {0};
    char owner[256] = {0};
    json_get_string(payload, "filename", filename, sizeof(filename));
    json_get_string(payload, "owner", owner, sizeof(owner));
    
    int rc = storage_engine_create_file(g_state.engine, filename, owner);
    
    if (rc == STORAGE_OK) {
        char response[256];
        snprintf(response, sizeof(response), "{\"status\":\"created\"}");
        send_message(client_fd, OP_COMMAND_STATUS, 0, response);
    } else {
        char error[256];
        snprintf(error, sizeof(error), "{\"error\":\"Failed to create file\",\"code\":%d}", rc);
        send_message(client_fd, OP_ERROR, 0, error);
    }
}

static void handle_createfolder_request(int client_fd, const char *payload) {
    char foldername[256] = {0};
    char username[256] = {0};
    json_get_string(payload, "foldername", foldername, sizeof(foldername));
    json_get_string(payload, "username", username, sizeof(username));
    
    int rc = storage_engine_create_folder(g_state.engine, foldername, username);
    
    if (rc == STORAGE_OK) {
        char response[256];
        snprintf(response, sizeof(response), "{\"status\":\"created\"}");
        send_message(client_fd, OP_COMMAND_STATUS, 0, response);
    } else if (rc == STORAGE_ERR_EXISTS) {
        send_message(client_fd, OP_ERROR, 0, "{\"error\":\"Folder already exists\"}");
    } else {
        char error[256];
        snprintf(error, sizeof(error), "{\"error\":\"Failed to create folder\",\"code\":%d}", rc);
        send_message(client_fd, OP_ERROR, 0, error);
    }
}

static void handle_move_request(int client_fd, const char *payload) {
    char filename[256] = {0};
    char foldername[256] = {0};
    json_get_string(payload, "filename", filename, sizeof(filename));
    json_get_string(payload, "foldername", foldername, sizeof(foldername));
    
    int rc = storage_engine_move_file(g_state.engine, filename, foldername);
    
    if (rc == STORAGE_OK) {
        char response[256];
        snprintf(response, sizeof(response), "{\"status\":\"moved\"}");
        send_message(client_fd, OP_COMMAND_STATUS, 0, response);
    } else if (rc == STORAGE_ERR_NOT_FOUND) {
        send_message(client_fd, OP_ERROR, 0, "{\"error\":\"File or folder not found\"}");
    } else {
        char error[256];
        snprintf(error, sizeof(error), "{\"error\":\"Failed to move file\",\"code\":%d}", rc);
        send_message(client_fd, OP_ERROR, 0, error);
    }
}

static void handle_viewfolder_request(int client_fd, const char *payload) {
    char foldername[256] = {0};
    char username[256] = {0};
    json_get_string(payload, "foldername", foldername, sizeof(foldername));
    json_get_string(payload, "username", username, sizeof(username));
    
    char **files = NULL;
    size_t file_count = 0;
    char **folders = NULL;
    size_t folder_count = 0;
    int rc = storage_engine_list_folder(g_state.engine,
                                        foldername,
                                        username,
                                        &files,
                                        &file_count,
                                        &folders,
                                        &folder_count);
    
    if (rc == STORAGE_OK) {
        char response[8192] = "{\"files\":[";
        size_t offset = strlen(response);
        
        for (size_t i = 0; i < file_count && offset < sizeof(response) - 100; i++) {
            if (i > 0) {
                offset += snprintf(response + offset, sizeof(response) - offset, ",");
            }
            offset += snprintf(response + offset, sizeof(response) - offset, "\"%s\"", files[i]);
            free(files[i]);
        }
        free(files);

        offset += snprintf(response + offset, sizeof(response) - offset, "],\"folders\":[");
        for (size_t i = 0; i < folder_count && offset < sizeof(response) - 100; i++) {
            if (i > 0) {
                offset += snprintf(response + offset, sizeof(response) - offset, ",");
            }
            offset += snprintf(response + offset, sizeof(response) - offset, "\"%s\"", folders[i]);
            free(folders[i]);
        }
        free(folders);
        snprintf(response + offset, sizeof(response) - offset, "]}");
        
        send_message(client_fd, OP_VIEWFOLDER_RESPONSE, 0, response);
    } else if (rc == STORAGE_ERR_NOT_FOUND) {
        send_message(client_fd, OP_ERROR, 0, "{\"error\":\"Folder not found\"}");
    } else if (rc == STORAGE_ERR_NOT_FOLDER) {
        send_message(client_fd, OP_ERROR, 0, "{\"error\":\"Not a folder\"}");
    } else if (rc == STORAGE_ERR_NO_ACCESS) {
        send_message(client_fd, OP_ERROR, 0, "{\"error\":\"Access denied - you do not own this folder\"}");
    } else {
        char error[256];
        snprintf(error, sizeof(error), "{\"error\":\"Failed to list folder\",\"code\":%d}", rc);
        send_message(client_fd, OP_ERROR, 0, error);
    }
}

static void handle_replicate_request(int client_fd, const char *payload) {
    char filename[256] = {0};
    char source_ip[64] = {0};
    unsigned long source_port = 0;
    
    json_get_string(payload, "filename", filename, sizeof(filename));
    json_get_string(payload, "source_ip", source_ip, sizeof(source_ip));
    source_port = json_get_ulong(payload, "source_port");
    
    ss_log("Replicating file '%s' from %s:%lu", filename, source_ip, source_port);
    
    /* Connect to source SS */
    int source_fd = tcp_connect(source_ip, (uint16_t)source_port);
    if (source_fd < 0) {
        char error[256];
        snprintf(error, sizeof(error), "{\"error\":\"Failed to connect to source SS\"}");
        send_message(client_fd, OP_ERROR, 0, error);
        return;
    }
    
    /* Request file content */
    char req[512];
    snprintf(req, sizeof(req), "{\"filename\":\"%s\"}", filename);
    if (send_message(source_fd, OP_DATA_REQUEST, 0, req) != 0) {
        close(source_fd);
        send_message(client_fd, OP_ERROR, 0, "{\"error\":\"Failed to send data request\"}");
        return;
    }
    
    /* Receive response */
    message_header_t hdr;
    char *resp_payload = NULL;
    if (recv_message(source_fd, &hdr, &resp_payload) != 0) {
        close(source_fd);
        send_message(client_fd, OP_ERROR, 0, "{\"error\":\"Failed to receive data response\"}");
        return;
    }
    close(source_fd);
    
    if (hdr.opcode == OP_DATA_RESPONSE) {
        /* Extract content manually to handle potentially large files */
        char *content_start = strstr(resp_payload, "\"content\":\"");
        if (content_start) {
            content_start += 11;
            /* Find the end quote - handle escaped quotes if necessary, but for now assume simple */
            char *content_end = strrchr(content_start, '"'); 
            /* Note: This simple parsing fails if content has escaped quotes at the end. 
               But our JSON generator doesn't escape quotes yet anyway. */
            
            if (content_end) {
                *content_end = '\0'; 
                
                /* Import to local storage */
                int rc = storage_engine_import(g_state.engine, filename, content_start);
                if (rc == STORAGE_OK) {
                    send_message(client_fd, OP_COMMAND_STATUS, 0, "{\"status\":\"replicated\"}");
                } else {
                    send_message(client_fd, OP_ERROR, 0, "{\"error\":\"Import failed\"}");
                }
            } else {
                send_message(client_fd, OP_ERROR, 0, "{\"error\":\"Invalid JSON format\"}");
            }
        } else {
            send_message(client_fd, OP_ERROR, 0, "{\"error\":\"No content in response\"}");
        }
    } else {
        send_message(client_fd, OP_ERROR, 0, "{\"error\":\"Source returned error\"}");
    }
    free(resp_payload);
}

static void *client_handler_thread(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);
    
    message_header_t header;
    char *payload = NULL;
    
    if (recv_message(client_fd, &header, &payload) != 0) {
        close(client_fd);
        return NULL;
    }
    
    switch (header.opcode) {
        case OP_COMMAND_FORWARD:
            handle_create_request(client_fd, payload);
            break;
        case OP_DATA_REQUEST:
            handle_read_request(client_fd, payload);
            break;
        case OP_WRITE_REQUEST:
            handle_write_request(client_fd, payload);
            break;
        case OP_STREAM_REQUEST:
            handle_stream_request(client_fd, payload);
            break;
        case OP_INFO_REQUEST:
            handle_info_request(client_fd, payload);
            break;
        case OP_UNDO_REQUEST:
            handle_undo_request(client_fd, payload);
            break;
        case OP_DELETE_REQUEST:
            handle_delete_request(client_fd, payload);
            break;
        case OP_CREATEFOLDER_REQUEST:
            handle_createfolder_request(client_fd, payload);
            break;
        case OP_MOVE_REQUEST:
            handle_move_request(client_fd, payload);
            break;
        case OP_VIEWFOLDER_REQUEST:
            handle_viewfolder_request(client_fd, payload);
            break;
        case OP_CHECKPOINT_REQUEST:
            handle_checkpoint_request(client_fd, payload);
            break;
        case OP_VIEWCHECKPOINT_REQUEST:
            handle_viewcheckpoint_request(client_fd, payload);
            break;
        case OP_REVERT_CHECKPOINT_REQUEST:
            handle_revertcheckpoint_request(client_fd, payload);
            break;
        case OP_LISTCHECKPOINTS_REQUEST:
            handle_listcheckpoints_request(client_fd, payload);
            break;
        case OP_REPLICATE_FILE:
            handle_replicate_request(client_fd, payload);
            break;
        case OP_COMMAND_STATUS:
            /* This is a status response, not a request. Ignore or log. */
            fprintf(stderr, "Received OP_COMMAND_STATUS unexpectedly (possible protocol mismatch)\n");
            break;
        default:
            fprintf(stderr, "Unknown opcode: %d\n", header.opcode);
            send_message(client_fd, OP_ERROR, 0, "{\"error\":\"Unknown operation\"}");
            break;
    }
    
    free(payload);
    close(client_fd);
    return NULL;
}

/* NM communication */
static int register_with_nm(void) {
    g_state.nm_fd = tcp_connect(g_state.nm_host, g_state.nm_port);
    if (g_state.nm_fd < 0) {
        perror("tcp_connect to NM");
        return -1;
    }
    
    char payload[1024];
    snprintf(payload, sizeof(payload),
             "{\"ss_id\":\"%s\",\"nm_ip\":\"%s\",\"client_port\":%u,"
             "\"capacity_bytes\":%u,\"active_load\":{\"readers\":0,\"writers\":0},"
             "\"file_manifest\":[]}",
             g_state.ss_id, g_state.nm_host, g_state.client_port, 268435456u);
    
    if (send_message(g_state.nm_fd, OP_REGISTER_SS, ++g_state.request_counter, payload) != 0) {
        close(g_state.nm_fd);
        g_state.nm_fd = -1;
        return -1;
    }
    
    message_header_t header;
    char *ack_payload = NULL;
    if (recv_message(g_state.nm_fd, &header, &ack_payload) != 0) {
        close(g_state.nm_fd);
        g_state.nm_fd = -1;
        return -1;
    }
    
    if (header.opcode != OP_REGISTER_ACK) {
        ss_log("Registration failed: %s", ack_payload ? ack_payload : "<no error>");
        free(ack_payload);
        close(g_state.nm_fd);
        g_state.nm_fd = -1;
        return -1;
    }
    
    ss_log("Registered with NM: %s", ack_payload ? ack_payload : "<empty>");
    free(ack_payload);
    return 0;
}

static void send_heartbeat(void) {
    if (g_state.nm_fd < 0) {
        return;
    }
    
    pthread_mutex_lock(&g_state.lock);
    unsigned int readers = g_state.active_readers;
    unsigned int writers = g_state.active_writers;
    pthread_mutex_unlock(&g_state.lock);
    
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"ss_id\":\"%s\",\"readers\":%u,\"writers\":%u}",
             g_state.ss_id, readers, writers);
    
    if (send_message(g_state.nm_fd, OP_HEARTBEAT, ++g_state.request_counter, payload) != 0) {
        ss_log("Heartbeat failed, reconnecting...");
        close(g_state.nm_fd);
        g_state.nm_fd = -1;
        /* Try to reconnect */
        register_with_nm();
    }
}

int main(int argc, char **argv) {
    if (argc != 5) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    memset(&g_state, 0, sizeof(g_state));
    strncpy(g_state.ss_id, argv[3], sizeof(g_state.ss_id) - 1);
    strncpy(g_state.nm_host, argv[1], sizeof(g_state.nm_host) - 1);
    g_state.nm_port = (uint16_t)strtoul(argv[2], NULL, 10);
    g_state.client_port = (uint16_t)strtoul(argv[4], NULL, 10);
    g_state.running = 1;
    g_state.nm_fd = -1;
    g_state.request_counter = 0;
    pthread_mutex_init(&g_state.lock, NULL);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Initialize storage engine */
    char storage_path[512];
    snprintf(storage_path, sizeof(storage_path), "./ss_data/%s", g_state.ss_id);
    if (storage_engine_init(&g_state.engine, storage_path, g_state.ss_id) != STORAGE_OK) {
        ss_log("Failed to initialize storage engine");
        return EXIT_FAILURE;
    }
    ss_log("Storage engine initialized at %s", storage_path);
    
    /* Register with Name Server */
    if (register_with_nm() != 0) {
        ss_log("Failed to register with Name Server");
        storage_engine_destroy(g_state.engine);
        return EXIT_FAILURE;
    }
    
    /* Create client-facing server socket */
    g_state.client_listen_fd = tcp_listen("0.0.0.0", g_state.client_port, 10);
    if (g_state.client_listen_fd < 0) {
        perror("tcp_listen");
        close(g_state.nm_fd);
        storage_engine_destroy(g_state.engine);
        return EXIT_FAILURE;
    }
    ss_log("Storage Server %s listening on port %u for clients", 
           g_state.ss_id, g_state.client_port);
    
    /* Main event loop */
    time_t last_heartbeat = time(NULL);
    const time_t HEARTBEAT_INTERVAL = 5;
    
    while (g_state.running) {
        /* Send periodic heartbeat */
        time_t now = time(NULL);
        if (now - last_heartbeat >= HEARTBEAT_INTERVAL) {
            send_heartbeat();
            last_heartbeat = now;
        }
        
        /* Accept client connections (non-blocking with timeout) */
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(g_state.client_listen_fd, &read_fds);
        
        struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
        int sel = select(g_state.client_listen_fd + 1, &read_fds, NULL, NULL, &tv);
        
        if (sel > 0 && FD_ISSET(g_state.client_listen_fd, &read_fds)) {
            int client_fd = accept(g_state.client_listen_fd, NULL, NULL);
            if (client_fd >= 0) {
                /* Spawn thread to handle client */
                int *fd_arg = malloc(sizeof(int));
                if (fd_arg != NULL) {
                    *fd_arg = client_fd;
                    pthread_t thread;
                    pthread_attr_t attr;
                    pthread_attr_init(&attr);
                    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
                    
                    if (pthread_create(&thread, &attr, client_handler_thread, fd_arg) != 0) {
                        perror("pthread_create");
                        free(fd_arg);
                        close(client_fd);
                    }
                    pthread_attr_destroy(&attr);
                } else {
                    close(client_fd);
                }
            }
        }
    }
    
    /* Cleanup */
    ss_log("Shutting down Storage Server %s...", g_state.ss_id);
    close(g_state.client_listen_fd);
    if (g_state.nm_fd >= 0) {
        close(g_state.nm_fd);
    }
    storage_engine_destroy(g_state.engine);
    pthread_mutex_destroy(&g_state.lock);
    
    return EXIT_SUCCESS;
}

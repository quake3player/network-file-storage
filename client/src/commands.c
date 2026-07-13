#define _POSIX_C_SOURCE 200809L

#include "commands.h"
#include "net.h"
#include "protocol.h"

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ANSI Color codes */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

int send_message(int fd, uint16_t opcode, uint32_t request_id, const char *payload) {
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

int recv_message(int fd, uint16_t *opcode_out, uint32_t *request_id_out, char **payload_out) {
    uint8_t header_buf[PROTOCOL_HEADER_SIZE];
    int rc = recv_all(fd, header_buf, sizeof(header_buf));
    if (rc <= 0) {
        return -1;
    }
    message_header_t header;
    if (protocol_decode_header(header_buf, &header) != 0) {
        return -1;
    }
    if (opcode_out) *opcode_out = header.opcode;
    if (request_id_out) *request_id_out = header.request_id;
    
    char *payload = NULL;
    if (header.payload_len > 0) {
        payload = calloc(1, header.payload_len + 1);
        if (payload == NULL) {
            return -1;
        }
        rc = recv_all(fd, payload, header.payload_len);
        if (rc <= 0) {
            free(payload);
            return -1;
        }
        payload[header.payload_len] = '\0';
    }
    if (payload_out) {
        *payload_out = payload;
    } else {
        free(payload);
    }
    return 0;
}

static int json_get_string(const char *json, const char *key, char *out, size_t out_len) {
    if (json == NULL || key == NULL || out == NULL || out_len == 0) {
        return -1;
    }
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (pos == NULL) {
        return -1;
    }
    pos += strlen(pattern);
    pos = strchr(pos, ':');
    if (pos == NULL) {
        return -1;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t') pos++;
    if (*pos != '"') {
        return -1;
    }
    pos++;
    const char *end = strchr(pos, '"');
    if (end == NULL) {
        return -1;
    }
    size_t len = (size_t)(end - pos);
    if (len >= out_len) {
        len = out_len - 1;
    }
    memcpy(out, pos, len);
    out[len] = '\0';
    return 0;
}

static int json_get_uint(const char *json, const char *key, unsigned long *out_value) {
    if (json == NULL || key == NULL || out_value == NULL) {
        return -1;
    }
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (pos == NULL) {
        return -1;
    }
    pos += strlen(pattern);
    pos = strchr(pos, ':');
    if (pos == NULL) {
        return -1;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t') pos++;
    char *endptr = NULL;
    errno = 0;
    unsigned long value = strtoul(pos, &endptr, 10);
    if (pos == endptr || errno != 0) {
        return -1;
    }
    *out_value = value;
    return 0;
}

/* Helper to get integer from JSON (can be negative) */
static int json_get_int(const char *json, const char *key, long *out_value) {
    if (json == NULL || key == NULL || out_value == NULL) {
        return -1;
    }
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (pos == NULL) {
        return -1;
    }
    pos += strlen(pattern);
    pos = strchr(pos, ':');
    if (pos == NULL) {
        return -1;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t') pos++;
    char *endptr = NULL;
    errno = 0;
    long value = strtol(pos, &endptr, 10);
    if (pos == endptr || errno != 0) {
        return -1;
    }
    *out_value = value;
    return 0;
}

/* Map storage engine error codes to human-readable messages */
static const char *get_error_message(long error_code) {
    switch (error_code) {
        case 0: return "Success";
        case -1: return "File not found";
        case -2: return "Invalid operation (check word/sentence indices)";
        case -3: return "Sentence is locked by another user";
        case -4: return "I/O error while accessing file";
        default: return "Unknown error";
    }
}

/* Parse error JSON and display nicely */
static void display_error(const char *json_error) {
    if (json_error == NULL) {
        fprintf(stderr, "%s❌ Unknown error occurred%s\n", COLOR_RED, COLOR_RESET);
        return;
    }
    
    char error_msg[512] = {0};
    char message[512] = {0};
    char code_str[128] = {0};
    long error_code = 0;
    
    /* Try different JSON field names that servers might use */
    json_get_string(json_error, "error", error_msg, sizeof(error_msg));
    json_get_string(json_error, "message", message, sizeof(message));
    json_get_string(json_error, "code", code_str, sizeof(code_str));
    json_get_int(json_error, "code", &error_code);
    
    /* Use whichever message field we found */
    const char *display_msg = error_msg[0] ? error_msg : (message[0] ? message : "Operation failed");
    
    fprintf(stderr, "\n%s❌ Error:%s %s\n", COLOR_RED, COLOR_RESET, display_msg);
    
    /* Show details only if we have a numeric error code */
    if (error_code != 0) {
        fprintf(stderr, "   %sDetails:%s %s (code: %ld)\n", COLOR_YELLOW, COLOR_RESET, get_error_message(error_code), error_code);
    } else if (code_str[0]) {
        fprintf(stderr, "   %sCode:%s %s\n", COLOR_YELLOW, COLOR_RESET, code_str);
    }
}

static size_t print_viewfolder_section(const char *json,
                                       const char *field,
                                       const char *label,
                                       const char *color,
                                       const char *icon) {
    if (json == NULL || field == NULL || label == NULL) {
        return 0;
    }

    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":[", field);
    const char *section = strstr(json, pattern);
    if (section == NULL) {
        return 0;
    }
    section += strlen(pattern);

    printf("  %s%s:%s\n", COLOR_BOLD, label, COLOR_RESET);

    size_t count = 0;
    const char *ptr = section;
    while (*ptr && *ptr != ']') {
        if (*ptr == '"') {
            ptr++;
            char name[256];
            size_t idx = 0;
            while (*ptr && *ptr != '"') {
                char ch = *ptr;
                if (ch == '\\' && *(ptr + 1) != '\0') {
                    ptr++;
                    ch = *ptr;
                }
                if (idx < sizeof(name) - 1) {
                    name[idx++] = ch;
                }
                ptr++;
            }
            name[idx] = '\0';
            if (*ptr == '"') {
                ptr++;
            }
            if (idx > 0) {
                printf("    %s%s %s%s\n", color, icon, name, COLOR_RESET);
                count++;
            }
        } else {
            ptr++;
        }
    }

    if (count == 0) {
        printf("    %s(empty)%s\n", COLOR_YELLOW, COLOR_RESET);
    }

    return count;
}

static int parse_mode_argument(const char *arg, const char *default_mode, char *out, size_t out_len) {
    if (out == NULL || out_len == 0) {
        return -1;
    }
    const char *source = (arg != NULL && arg[0] != '\0') ? arg : default_mode;
    if (source == NULL) {
        return -1;
    }
    char lower[16];
    size_t len = strlen(source);
    if (len >= sizeof(lower)) {
        len = sizeof(lower) - 1;
    }
    for (size_t i = 0; i < len; i++) {
        lower[i] = (char)tolower((unsigned char)source[i]);
    }
    lower[len] = '\0';
    if (strcmp(lower, "r") == 0 || strcmp(lower, "read") == 0) {
        strncpy(out, "read", out_len - 1);
        out[out_len - 1] = '\0';
        return 0;
    }
    if (strcmp(lower, "w") == 0 || strcmp(lower, "write") == 0) {
        strncpy(out, "write", out_len - 1);
        out[out_len - 1] = '\0';
        return 0;
    }
    return -1;
}

static void format_timestamp(unsigned long epoch, char *out, size_t out_len) {
    if (out == NULL || out_len == 0) {
        return;
    }
    if (epoch == 0) {
        snprintf(out, out_len, "unknown");
        return;
    }
    time_t t = (time_t)epoch;
    struct tm tm_snapshot;
    if (localtime_r(&t, &tm_snapshot) == NULL) {
        snprintf(out, out_len, "unknown");
        return;
    }
    strftime(out, out_len, "%Y-%m-%d %H:%M", &tm_snapshot);
}

/* Helper wrapper for recv_message with opcode check */
static int recv_with_opcode(int fd, uint16_t *opcode_out, char **payload_out) {
    return recv_message(fd, opcode_out, NULL, payload_out);
}

int cmd_view(client_context_t *ctx, int argc, char **argv) {
    /* Parse flags */
    int show_all = 0;
    int show_details = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "-al") == 0 || strcmp(argv[i], "-la") == 0) {
            show_all = 1;
        }
        if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "-al") == 0 || strcmp(argv[i], "-la") == 0) {
            show_details = 1;
        }
    }
    
    /* Connect to NM to get file list */
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"command\":\"VIEW\",\"username\":\"%s\",\"show_all\":%d,\"show_details\":%d}",
             ctx->username, show_all, show_details);
    
    if (send_message(nm_fd, OP_COMMAND_FORWARD, 0, payload) != 0) {
        close(nm_fd);
        return 0;
    }
    
    uint16_t opcode = 0;
    char *resp = NULL;
    if (recv_with_opcode(nm_fd, &opcode, &resp) != 0) {
        close(nm_fd);
        return 0;
    }
    close(nm_fd);
    
    if (opcode == OP_ERROR) {
        display_error(resp);
        free(resp);
        return 0;
    }
    
    /* Display the file list */
    if (show_details) {
        printf("\n");
        printf("%s══════════════════════════════════════════════════════════════%s\n", COLOR_CYAN, COLOR_RESET);
        printf("%s  Filename       │ Words │ Chars │   Last Access   │  Owner  %s\n", COLOR_CYAN, COLOR_RESET);
        printf("%s══════════════════════════════════════════════════════════════%s\n", COLOR_CYAN, COLOR_RESET);
    }
    
    printf("%s\n", resp ? resp : "(no files)");
    
    if (show_details) {
        printf("%s══════════════════════════════════════════════════════════════%s\n", COLOR_CYAN, COLOR_RESET);
    }
    
    free(resp);
    return 0;
}

int cmd_read(client_context_t *ctx, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: READ <filename>\n");
        return 0;  // Don't exit client
    }
    const char *filename = argv[1];
    
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;  // Don't exit client
    }
    
    char lookup_payload[512];
    snprintf(lookup_payload, sizeof(lookup_payload),
             "{\"filename\":\"%s\",\"username\":\"%s\",\"operation\":\"read\"}",
             filename, ctx->username);
    
    if (send_message(nm_fd, OP_LOOKUP_FILE, 1, lookup_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to send lookup request%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;  // Don't exit client
    }
    
    uint16_t opcode;
    char *resp_payload = NULL;
    if (recv_message(nm_fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to receive lookup response%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;  // Don't exit client
    }
    close(nm_fd);
    
    if (opcode == OP_ERROR) {
        display_error(resp_payload);
        free(resp_payload);
        return 0;  // Don't exit client
    }
    
    if (opcode != OP_LOOKUP_RESP) {
        fprintf(stderr, "%s⚠️  Unexpected response from Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        free(resp_payload);
        return 0;  // Don't exit client
    }
    
    /* Parse SS info from lookup response */
    char ss_host[256] = {0};
    unsigned long ss_port_ul = 0;
    
    /* Structure to hold replica info */
    struct {
        char ip[64];
        unsigned long port;
        int fd;
    } replicas[8];
    /* Initialize fds to -1 to avoid closing random file descriptors (like stdin) later */
    for(int i=0; i<8; i++) replicas[i].fd = -1;
    
    int replica_count = 0;

    if (resp_payload != NULL) {
        json_get_string(resp_payload, "ss_ip", ss_host, sizeof(ss_host));
        json_get_uint(resp_payload, "ss_port", &ss_port_ul);
        
        /* Parse replicas for failover */
        const char *rep_start = strstr(resp_payload, "\"replicas\":[");
        if (rep_start != NULL) {
            rep_start += 12; /* Skip "replicas":[ */
            const char *ptr = rep_start;
            while (*ptr && *ptr != ']' && replica_count < 8) {
                if (*ptr == '{') {
                    char rep_ip[64] = {0};
                    unsigned long rep_port = 0;
                    
                    const char *ip_key = strstr(ptr, "\"ip\":\"");
                    if (ip_key) {
                        ip_key += 6;
                        const char *ip_end = strchr(ip_key, '"');
                        if (ip_end) {
                            size_t len = ip_end - ip_key;
                            if (len < sizeof(rep_ip)) {
                                memcpy(rep_ip, ip_key, len);
                                rep_ip[len] = '\0';
                            }
                        }
                    }
                    
                    const char *port_key = strstr(ptr, "\"port\":");
                    if (port_key) {
                        port_key += 7;
                        rep_port = strtoul(port_key, NULL, 10);
                    }
                    
                    if (rep_ip[0] != '\0' && rep_port > 0) {
                        strncpy(replicas[replica_count].ip, rep_ip, sizeof(replicas[replica_count].ip));
                        replicas[replica_count].port = rep_port;
                        replica_count++;
                    }
                }
                ptr++;
            }
        }
        
        free(resp_payload);
        resp_payload = NULL;
    }
    
    if (ss_port_ul == 0) {
        fprintf(stderr, "⚠ Invalid storage server information from lookup\n");
        return 0;  // Don't exit client
    }
    
    /* Step 2: Connect to SS and request file content */
    int ss_fd = tcp_connect(ss_host, (uint16_t)ss_port_ul);
    if (ss_fd < 0) {
        fprintf(stderr, "⚠ Failed to connect to Primary Storage Server at %s:%lu\n", ss_host, ss_port_ul);
        
        /* Failover to replicas */
        int connected = 0;
        for (int i = 0; i < replica_count; i++) {
            printf("   %sAttempting failover to replica %d (%s:%lu)...%s\n", 
                   COLOR_YELLOW, i+1, replicas[i].ip, replicas[i].port, COLOR_RESET);
            ss_fd = tcp_connect(replicas[i].ip, (uint16_t)replicas[i].port);
            if (ss_fd >= 0) {
                printf("   %sConnected to replica!%s\n", COLOR_GREEN, COLOR_RESET);
                connected = 1;
                break;
            }
        }
        
        if (!connected) {
            fprintf(stderr, "❌ Failed to connect to any Storage Server (Primary or Replicas)\n");
            return 0;
        }
    }
    
    char read_payload[512];
    snprintf(read_payload, sizeof(read_payload),
             "{\"filename\":\"%s\",\"operation\":\"read\",\"username\":\"%s\"}",
             filename, ctx->username);
    
    if (send_message(ss_fd, OP_DATA_REQUEST, 2, read_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to send read request to Storage Server%s\n", COLOR_YELLOW, COLOR_RESET);
        close(ss_fd);
        return 0;  // Don't exit client
    }
    
    if (recv_message(ss_fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to receive data from Storage Server%s\n", COLOR_YELLOW, COLOR_RESET);
        close(ss_fd);
        return 0;  // Don't exit client
    }
    close(ss_fd);
    
    if (opcode == OP_ERROR) {
        display_error(resp_payload);
        free(resp_payload);
        return 0;  // Don't exit client
    }
    
    if (resp_payload != NULL) {
        /* Extract content from JSON response */
        char content[8192] = {0};
        int is_json = (json_get_string(resp_payload, "content", content, sizeof(content)) == 0);
        
        printf("\n%s📖 Reading '%s':%s\n", COLOR_CYAN, filename, COLOR_RESET);
        printf("%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n", COLOR_CYAN, COLOR_RESET);
        
        if (is_json) {
            printf("%s\n", content);
        } else {
            /* Fallback: print raw if not JSON */
            printf("%s\n", resp_payload);
        }
        
        printf("%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n", COLOR_CYAN, COLOR_RESET);
        free(resp_payload);
    }
    
    return 0;
}

int cmd_create(client_context_t *ctx, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: CREATE <filename>\n");
        return 0;
    }
    const char *filename = argv[1];
    
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"filename\":\"%s\",\"username\":\"%s\",\"command\":\"CREATE\"}",
             filename, ctx->username);
    
    if (send_message(nm_fd, OP_COMMAND_FORWARD, 3, payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to send CREATE command%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    
    uint16_t opcode;
    char *resp_payload = NULL;
    if (recv_message(nm_fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to receive response%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    close(nm_fd);
    
    if (opcode == OP_ERROR) {
        display_error(resp_payload);
        free(resp_payload);
        return 0;
    }
    
    printf("%s✅ File '%s' created successfully!%s\n", COLOR_GREEN, filename, COLOR_RESET);
    free(resp_payload);
    return 0;
}

int cmd_write(client_context_t *ctx, int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: WRITE <filename> <sentence_index>\n");
        return 0;  // Don't exit client
    }
    const char *filename = argv[1];
    unsigned long sentence_idx = strtoul(argv[2], NULL, 10);
    
    /* Step 1: Lookup file via NM */
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "⚠ Failed to connect to Name Server\n");
        return 0;  // Don't exit client
    }
    
    char lookup_payload[512];
    snprintf(lookup_payload, sizeof(lookup_payload),
             "{\"filename\":\"%s\",\"username\":\"%s\",\"operation\":\"write\"}",
             filename, ctx->username);
    
    if (send_message(nm_fd, OP_LOOKUP_FILE, 0, lookup_payload) != 0) {
        close(nm_fd);
        fprintf(stderr, "⚠ Failed to send lookup request\n");
        return 0;  // Don't exit client
    }
    
    uint16_t lookup_opcode = 0;
    char *lookup_resp = NULL;
    if (recv_with_opcode(nm_fd, &lookup_opcode, &lookup_resp) != 0) {
        close(nm_fd);
        fprintf(stderr, "⚠ Failed to receive lookup response\n");
        return 0;  // Don't exit client
    }
    close(nm_fd);
    
    if (lookup_opcode == OP_ERROR) {
        display_error(lookup_resp);
        free(lookup_resp);
        return 0;  // Don't exit client
    }
    
    /* Parse SS info from lookup response */
    char ss_host[256] = {0};
    unsigned long ss_port_ul = 0;
    
    /* Structure to hold replica info */
    struct {
        char ip[64];
        unsigned long port;
        int fd;
    } replicas[8];
    /* Initialize fds to -1 to avoid closing random file descriptors (like stdin) later */
    for(int i=0; i<8; i++) replicas[i].fd = -1;
    
    int replica_count = 0;

    if (lookup_resp != NULL) {
        json_get_string(lookup_resp, "ss_ip", ss_host, sizeof(ss_host));
        json_get_uint(lookup_resp, "ss_port", &ss_port_ul);
        
        /* Parse replicas for failover */
        const char *rep_start = strstr(lookup_resp, "\"replicas\":[");
        if (rep_start != NULL) {
            rep_start += 12; /* Skip "replicas":[ */
            const char *ptr = rep_start;
            while (*ptr && *ptr != ']' && replica_count < 8) {
                if (*ptr == '{') {
                    char rep_ip[64] = {0};
                    unsigned long rep_port = 0;
                    
                    const char *ip_key = strstr(ptr, "\"ip\":\"");
                    if (ip_key) {
                        ip_key += 6;
                        const char *ip_end = strchr(ip_key, '"');
                        if (ip_end) {
                            size_t len = ip_end - ip_key;
                            if (len < sizeof(rep_ip)) {
                                memcpy(rep_ip, ip_key, len);
                                rep_ip[len] = '\0';
                            }
                        }
                    }
                    
                    const char *port_key = strstr(ptr, "\"port\":");
                    if (port_key) {
                        port_key += 7;
                        rep_port = strtoul(port_key, NULL, 10);
                    }
                    
                    if (rep_ip[0] != '\0' && rep_port > 0) {
                        strncpy(replicas[replica_count].ip, rep_ip, sizeof(replicas[replica_count].ip));
                        replicas[replica_count].port = rep_port;
                        replica_count++;
                    }
                }
                ptr++;
            }
        }
        
        free(lookup_resp);
    }
    
    if (ss_port_ul == 0) {
        fprintf(stderr, "⚠ Invalid storage server information from lookup\n");
        return 0;  // Don't exit client
    }
    
    /* Step 2: Connect to SS and request write lock */
    int ss_fd = tcp_connect(ss_host, (uint16_t)ss_port_ul);
    if (ss_fd < 0) {
        fprintf(stderr, "⚠ Failed to connect to Storage Server at %s:%lu\n", ss_host, ss_port_ul);
        
        /* Failover to replicas */
        int connected = 0;
        for (int i = 0; i < replica_count; i++) {
            printf("   %sAttempting failover to replica %d (%s:%lu)...%s\n", 
                   COLOR_YELLOW, i+1, replicas[i].ip, replicas[i].port, COLOR_RESET);
            ss_fd = tcp_connect(replicas[i].ip, (uint16_t)replicas[i].port);
            if (ss_fd >= 0) {
                printf("   %sConnected to replica!%s\n", COLOR_GREEN, COLOR_RESET);
                connected = 1;
                break;
            }
        }
        
        if (!connected) {
            fprintf(stderr, "❌ Failed to connect to any Storage Server (Primary or Replicas)\n");
            return 0;
        }
    }
    
    char write_req[512];
    snprintf(write_req, sizeof(write_req),
             "{\"filename\":\"%s\",\"sentence\":%lu,\"username\":\"%s\"}",
             filename, sentence_idx, ctx->username);
    
    if (send_message(ss_fd, OP_WRITE_REQUEST, 0, write_req) != 0) {
        close(ss_fd);
        fprintf(stderr, "⚠ Failed to send write request\n");
        return 0;  // Don't exit client
    }
    
    /* Wait for lock acknowledgment from Primary */
    uint16_t ack_opcode = 0;
    char *ack_payload = NULL;
    if (recv_with_opcode(ss_fd, &ack_opcode, &ack_payload) != 0) {
        close(ss_fd);
        fprintf(stderr, "⚠ Failed to receive lock acknowledgment\n");
        return 0;  // Don't exit client
    }
    
    if (ack_opcode == OP_ERROR) {
        /* Parse and display error immediately */
        display_error(ack_payload);
        free(ack_payload);
        close(ss_fd);
        return 0;  // Don't exit client
    }
    
    /* Extract word count from lock acknowledgment */
    unsigned long current_word_count = 0;
    if (ack_payload != NULL) {
        json_get_uint(ack_payload, "word_count", &current_word_count);
    }
    free(ack_payload);
    
    printf("%s✓ Acquired write lock%s for '%s' sentence %lu\n", COLOR_GREEN, COLOR_RESET, filename, sentence_idx);
    if (replica_count > 0) {
        int active_reps = 0;
        for(int i=0; i<replica_count; i++) if(replicas[i].fd >= 0) active_reps++;
        printf("   %s(Replicating to %d backup servers)%s\n", COLOR_CYAN, active_reps, COLOR_RESET);
    }
    printf("   Current word count: %s%lu%s\n", COLOR_CYAN, current_word_count, COLOR_RESET);
    printf("Enter updates in format: %s<word_index> <content>%s\n", COLOR_CYAN, COLOR_RESET);
    printf("Type '%sETIRW%s' to commit and release lock\n", COLOR_BOLD, COLOR_RESET);
    
    /* Step 3: Collect and send word updates */
    char line[1024];
    int committed = 0;
    int updates_sent = 0;  /* Track if any valid updates were sent */
    
    while (1) {
        printf("> ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }
        
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        
        if (strcmp(line, "ETIRW") == 0) {
            /* Check if any updates were sent */
            if (updates_sent == 0) {
                printf("\n%s⚠️  No updates were made%s\n", COLOR_YELLOW, COLOR_RESET);
                /* Don't commit, just break and release lock */
                break;
            }
            
            /* Send commit message to Primary */
            if (send_message(ss_fd, OP_WRITE_COMMIT, 0, "{}") == 0) {
                /* Send commit to Replicas */
                for (int i = 0; i < replica_count; i++) {
                    if (replicas[i].fd >= 0) {
                        send_message(replicas[i].fd, OP_WRITE_COMMIT, 0, "{}");
                        /* We don't wait for replica commit ACK */
                    }
                }

                /* Wait for commit acknowledgment from Primary */
                uint16_t commit_opcode = 0;
                char *commit_resp = NULL;
                if (recv_with_opcode(ss_fd, &commit_opcode, &commit_resp) == 0) {
                    if (commit_opcode == OP_WRITE_ACK) {
                        printf("\n%s✅ Write committed successfully!%s\n", COLOR_GREEN, COLOR_RESET);
                        if (commit_resp != NULL) {
                            char undo_token[64] = {0};
                            json_get_string(commit_resp, "undo_token", undo_token, sizeof(undo_token));
                            if (strlen(undo_token) > 0) {
                                printf("   %sUndo token:%s %s\n", COLOR_CYAN, COLOR_RESET, undo_token);
                            }
                        }
                        committed = 1;
                    } else {
                        /* Parse and display error nicely */
                        display_error(commit_resp);
                        fprintf(stderr, "%s💾 Changes were NOT saved to file%s\n", COLOR_YELLOW, COLOR_RESET);
                        /* Don't set committed = 1, will show "aborted" message */
                    }
                    free(commit_resp);
                } else {
                    fprintf(stderr, "❌ Failed to receive commit response from server\n");
                }
            } else {
                fprintf(stderr, "❌ Failed to send commit message to server\n");
            }
            
            /* Clear any remaining input in stdin buffer to prevent infinite loop in main loop */
            /* This happens if user pasted multiple lines including ETIRW */
            /* We don't want to consume them here, but we need to ensure main loop handles them */
            break;  /* Always exit the loop after ETIRW, regardless of success/failure */
        }
        
        /* Skip empty lines */
        if (strlen(line) == 0) {
            continue;
        }
        
        /* Parse word update: <word_index> <content> */
        char *space = strchr(line, ' ');
        if (space == NULL) {
            fprintf(stderr, "%s⚠️  Invalid format.%s Expected: <word_index> <content>\n", COLOR_YELLOW, COLOR_RESET);
            fprintf(stderr, "   %sExample:%s 0 Hello World\n", COLOR_CYAN, COLOR_RESET);
            continue;
        }
        
        *space = '\0';
        char *endptr = NULL;
        unsigned long word_idx = strtoul(line, &endptr, 10);
        const char *content = space + 1;
        
        /* Validate that the word index is actually a number */
        if (endptr == line || *endptr != '\0') {
            fprintf(stderr, "%s⚠️  Invalid word index '%s'.%s Must be a number.\n", COLOR_YELLOW, line, COLOR_RESET);
            fprintf(stderr, "   %sExample:%s 0 Hello World\n", COLOR_CYAN, COLOR_RESET);
            *space = ' ';  /* Restore the space for error message */
            continue;
        }
        
        /* Validate word index immediately */
        if (word_idx > current_word_count) {
            fprintf(stderr, "\n%s❌ Error:%s Word index out of range\n", COLOR_RED, COLOR_RESET);
            fprintf(stderr, "   %sDetails:%s Word index must be between 0 and %lu (current word count)\n", 
                    COLOR_YELLOW, COLOR_RESET, current_word_count);
            fprintf(stderr, "   Use index %lu to append at the end\n", current_word_count);
            continue;  /* Skip this update, allow more input */
        }
        
        /* Send word update to SS */
        char update_payload[1024];
        snprintf(update_payload, sizeof(update_payload),
                "{\"word_index\":%lu,\"content\":\"%s\"}",
                word_idx, content);
        
        if (send_message(ss_fd, OP_WRITE_UPDATE, 0, update_payload) != 0) {
            fprintf(stderr, "⚠ Failed to send word update (connection issue)\n");
            fprintf(stderr, "⚠ Write session terminated - changes NOT saved\n");
            break;
        }
        
        /* Send update to Replicas */
        for (int i = 0; i < replica_count; i++) {
            if (replicas[i].fd >= 0) {
                send_message(replicas[i].fd, OP_WRITE_UPDATE, 0, update_payload);
            }
        }
        
        updates_sent++;  /* Increment counter for successful updates */
        printf("  %s✓%s Update queued: word[%lu] = \"%s\"\n", COLOR_GREEN, COLOR_RESET, word_idx, content);
        
        /* Update word count for next validation - account for potential word additions */
        /* Count words in the content added */
        char temp[1024];
        strncpy(temp, content, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = '\0';
        size_t words_added = 0;
        char *token = strtok(temp, " \t\n");
        while (token != NULL) {
            words_added++;
            token = strtok(NULL, " \t\n");
        }
        
        /* If inserting at position <= current_word_count, total increases by words_added */
        current_word_count += words_added;
    }
    
    close(ss_fd);
    for (int i = 0; i < replica_count; i++) if (replicas[i].fd >= 0) close(replicas[i].fd);
    
    if (!committed) {
        if (updates_sent == 0) {
            printf("%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n\n", COLOR_BLUE, COLOR_RESET);
        } else {
            printf("\n%s⚠️  Write session aborted%s (lock released, changes NOT saved)\n", COLOR_YELLOW, COLOR_RESET);
            printf("%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n\n", COLOR_BLUE, COLOR_RESET);
        }
        return 0;  // Don't terminate client session, just return to prompt
    }
    
    /* Add visual separation after successful write */
    printf("%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n", COLOR_GREEN, COLOR_RESET);
    
    (void)ctx;
    return 0;
}

int cmd_delete(client_context_t *ctx, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: DELETE <filename>\n");
        return 0;
    }
    const char *filename = argv[1];
    
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"filename\":\"%s\",\"username\":\"%s\",\"command\":\"DELETE\"}",
             filename, ctx->username);
    
    if (send_message(nm_fd, OP_COMMAND_FORWARD, 4, payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to send DELETE command%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    
    uint16_t opcode;
    char *resp_payload = NULL;
    if (recv_message(nm_fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to receive response%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    close(nm_fd);
    
    if (opcode == OP_ERROR) {
        display_error(resp_payload);
        free(resp_payload);
        return 0;
    }
    
    printf("%s✅ File '%s' deleted successfully!%s\n", COLOR_GREEN, filename, COLOR_RESET);
    free(resp_payload);
    return 0;
}

int cmd_undo(client_context_t *ctx, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: UNDO <filename>\n");
        return 0;
    }
    const char *filename = argv[1];
    
    /* Lookup file via NM to get SS info */
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    char lookup_payload[512];
    snprintf(lookup_payload, sizeof(lookup_payload),
             "{\"filename\":\"%s\",\"username\":\"%s\"}",
             filename, ctx->username);
    
    if (send_message(nm_fd, OP_LOOKUP_FILE, 0, lookup_payload) != 0) {
        close(nm_fd);
        return 0;
    }
    
    uint16_t lookup_opcode = 0;
    char *lookup_resp = NULL;
    if (recv_with_opcode(nm_fd, &lookup_opcode, &lookup_resp) != 0) {
        close(nm_fd);
        return 0;
    }
    close(nm_fd);
    
    if (lookup_opcode == OP_ERROR) {
        display_error(lookup_resp);
        free(lookup_resp);
        return 0;
    }
    
    char ss_host[256] = {0};
    unsigned long ss_port_ul = 0;
    char owner_from_nm[128] = {0};
    if (lookup_resp != NULL) {
        json_get_string(lookup_resp, "ss_ip", ss_host, sizeof(ss_host));
        json_get_uint(lookup_resp, "ss_port", &ss_port_ul);
        json_get_string(lookup_resp, "owner", owner_from_nm, sizeof(owner_from_nm));
        free(lookup_resp);
    }
    
    if (ss_port_ul == 0) {
        fprintf(stderr, "%s⚠️  Invalid storage server information%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    /* Connect to SS and request undo */
    int ss_fd = tcp_connect(ss_host, (uint16_t)ss_port_ul);
    if (ss_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Storage Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    char undo_payload[512];
    snprintf(undo_payload, sizeof(undo_payload),
             "{\"filename\":\"%s\",\"username\":\"%s\"}",
             filename, ctx->username);
    
    if (send_message(ss_fd, OP_UNDO_REQUEST, 0, undo_payload) != 0) {
        close(ss_fd);
        return 0;
    }
    
    uint16_t opcode = 0;
    char *resp = NULL;
    if (recv_with_opcode(ss_fd, &opcode, &resp) != 0) {
        close(ss_fd);
        return 0;
    }
    close(ss_fd);
    
    if (opcode == OP_ERROR) {
        display_error(resp);
        free(resp);
        return 0;
    }
    
    printf("%s✅ Undo operation completed for '%s'%s\n", COLOR_GREEN, filename, COLOR_RESET);
    free(resp);
    (void)ctx;
    return 0;
}

int cmd_info(client_context_t *ctx, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: INFO <filename>\n");
        return 0;
    }
    const char *filename = argv[1];
    
    /* Lookup file via NM */
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    char lookup_payload[512];
    snprintf(lookup_payload, sizeof(lookup_payload),
             "{\"filename\":\"%s\",\"username\":\"%s\"}",
             filename, ctx->username);
    
    if (send_message(nm_fd, OP_LOOKUP_FILE, 0, lookup_payload) != 0) {
        close(nm_fd);
        return 0;
    }
    
    uint16_t lookup_opcode = 0;
    char *lookup_resp = NULL;
    if (recv_with_opcode(nm_fd, &lookup_opcode, &lookup_resp) != 0) {
        close(nm_fd);
        return 0;
    }
    close(nm_fd);
    
    if (lookup_opcode == OP_ERROR) {
        display_error(lookup_resp);
        free(lookup_resp);
        return 0;
    }
    
    char ss_host[256] = {0};
    unsigned long ss_port_ul = 0;
    if (lookup_resp != NULL) {
        json_get_string(lookup_resp, "ss_ip", ss_host, sizeof(ss_host));
        json_get_uint(lookup_resp, "ss_port", &ss_port_ul);
        free(lookup_resp);
    }
    
    if (ss_port_ul == 0) {
        fprintf(stderr, "%s⚠️  Invalid storage server information%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    /* Connect to SS and request info */
    int ss_fd = tcp_connect(ss_host, (uint16_t)ss_port_ul);
    if (ss_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Storage Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    char info_payload[512];
    snprintf(info_payload, sizeof(info_payload),
             "{\"filename\":\"%s\",\"username\":\"%s\"}",
             filename, ctx->username);
    
    if (send_message(ss_fd, OP_INFO_REQUEST, 0, info_payload) != 0) {
        close(ss_fd);
        return 0;
    }
    
    uint16_t opcode = 0;
    char *resp = NULL;
    if (recv_with_opcode(ss_fd, &opcode, &resp) != 0) {
        close(ss_fd);
        return 0;
    }
    close(ss_fd);
    
    if (opcode == OP_ERROR) {
        display_error(resp);
        free(resp);
        return 0;
    }

    unsigned long word_count = 0;
    unsigned long char_count = 0;
    unsigned long sentence_count = 0;
    unsigned long created_at = 0;
    unsigned long modified_at = 0;
    unsigned long last_accessed = 0;
    char owner_from_ss[128] = {0};
    char last_accessed_by[128] = {0};
    if (resp != NULL) {
        json_get_string(resp, "owner", owner_from_ss, sizeof(owner_from_ss));
        json_get_uint(resp, "word_count", &word_count);
        json_get_uint(resp, "char_count", &char_count);
        json_get_uint(resp, "sentence_count", &sentence_count);
        json_get_uint(resp, "created_at", &created_at);
        json_get_uint(resp, "modified_at", &modified_at);
        json_get_uint(resp, "last_accessed", &last_accessed);
        json_get_string(resp, "last_accessed_by", last_accessed_by, sizeof(last_accessed_by));
        free(resp);
    }

    /* Fetch ACL info from Name Server */
    char *acl_text = NULL;
    char owner_from_nm[128] = {0};
    int acl_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (acl_fd >= 0) {
        char acl_payload[512];
        snprintf(acl_payload, sizeof(acl_payload),
                 "{\"filename\":\"%s\",\"username\":\"%s\",\"command\":\"ACLINFO\"}",
                 filename, ctx->username);
        if (send_message(acl_fd, OP_COMMAND_FORWARD, 0, acl_payload) == 0) {
            uint16_t acl_opcode = 0;
            if (recv_with_opcode(acl_fd, &acl_opcode, &acl_text) != 0) {
                acl_text = NULL;
            } else if (acl_opcode == OP_ERROR) {
                display_error(acl_text);
                free(acl_text);
                acl_text = NULL;
            } else if (acl_text != NULL) {
                /* Extract owner from first line of ACL (format: "owner (RW)") */
                char *space = strchr(acl_text, ' ');
                if (space != NULL) {
                    size_t len = space - acl_text;
                    if (len > sizeof(owner_from_nm) - 1) {
                        len = sizeof(owner_from_nm) - 1;
                    }
                    strncpy(owner_from_nm, acl_text, len);
                    owner_from_nm[len] = '\0';
                }
            }
        }
        close(acl_fd);
    }

    char created_str[64];
    char modified_str[64];
    char accessed_str[64];
    format_timestamp(created_at, created_str, sizeof(created_str));
    format_timestamp(modified_at, modified_str, sizeof(modified_str));
    format_timestamp(last_accessed, accessed_str, sizeof(accessed_str));

    /* Use owner from Name Server if available, otherwise fall back to Storage Server */
    const char *display_owner = owner_from_nm[0] ? owner_from_nm : 
                                (owner_from_ss[0] && strcmp(owner_from_ss, "unknown") != 0) ? owner_from_ss : "(none)";

    /* Display file information in a nice box */
    printf("\n%s╔════════════════════════════════════════════════════╗%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s║              📄 File Information                   ║%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s╠════════════════════════════════════════════════════╣%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s║%s %-50s %s║%s\n", COLOR_CYAN, COLOR_RESET, filename, COLOR_CYAN, COLOR_RESET);
    printf("%s╠════════════════════════════════════════════════════╣%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s║%s Owner:          %-33s %s║%s\n", COLOR_CYAN, COLOR_RESET, 
           display_owner, COLOR_CYAN, COLOR_RESET);
    printf("%s║%s Created:        %-33s %s║%s\n", COLOR_CYAN, COLOR_RESET, created_str, COLOR_CYAN, COLOR_RESET);
    printf("%s║%s Last Modified:  %-33s %s║%s\n", COLOR_CYAN, COLOR_RESET, modified_str, COLOR_CYAN, COLOR_RESET);
    
    /* Last accessed info */
    if (last_accessed_by[0] != '\0') {
        char accessed_full[256];
        snprintf(accessed_full, sizeof(accessed_full), "%s by %s", accessed_str, last_accessed_by);
        printf("%s║%s Last Accessed:  %-33s %s║%s\n", COLOR_CYAN, COLOR_RESET, accessed_full, COLOR_CYAN, COLOR_RESET);
    } else {
        printf("%s║%s Last Accessed:  %-33s %s║%s\n", COLOR_CYAN, COLOR_RESET, accessed_str, COLOR_CYAN, COLOR_RESET);
    }
    
    printf("%s╠════════════════════════════════════════════════════╣%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s║%s Words: %-10lu  Characters: %-10lu  Sentences: %-4lu %s║%s\n", 
           COLOR_CYAN, COLOR_RESET, word_count, char_count, sentence_count, COLOR_CYAN, COLOR_RESET);
    printf("%s╠════════════════════════════════════════════════════╣%s\n", COLOR_CYAN, COLOR_RESET);
    
    /* Access Control List */
    printf("%s║%s %sAccess Control:%s                                     %s║%s\n", 
           COLOR_CYAN, COLOR_RESET, COLOR_BOLD, COLOR_RESET, COLOR_CYAN, COLOR_RESET);
    if (acl_text != NULL && acl_text[0] != '\0') {
        char *acl_copy = strdup(acl_text);
        if (acl_copy != NULL) {
            char *line = strtok(acl_copy, "\n");
            while (line != NULL) {
                if (*line != '\0') {
                    printf("%s║%s   %-46s %s║%s\n", COLOR_CYAN, COLOR_RESET, line, COLOR_CYAN, COLOR_RESET);
                }
                line = strtok(NULL, "\n");
            }
            free(acl_copy);
        }
    } else {
        char owner_only[256];
        snprintf(owner_only, sizeof(owner_only), "%s (owner only)", 
                 owner_from_ss[0] ? owner_from_ss : "owner");
        printf("%s║%s   %-46s %s║%s\n", COLOR_CYAN, COLOR_RESET, owner_only, COLOR_CYAN, COLOR_RESET);
    }
    printf("%s╚════════════════════════════════════════════════════╝%s\n", COLOR_CYAN, COLOR_RESET);
    
    if (acl_text != NULL) {
        free(acl_text);
    }

    (void)ctx;
    return 0;
}

int cmd_stream(client_context_t *ctx, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: STREAM <filename>\n");
        return 0;
    }
    const char *filename = argv[1];
    
    /* Lookup file via NM */
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    char lookup_payload[512];
    snprintf(lookup_payload, sizeof(lookup_payload),
             "{\"filename\":\"%s\",\"username\":\"%s\"}",
             filename, ctx->username);
    
    if (send_message(nm_fd, OP_LOOKUP_FILE, 0, lookup_payload) != 0) {
        close(nm_fd);
        return 0;
    }
    
    uint16_t lookup_opcode = 0;
    char *lookup_resp = NULL;
    if (recv_with_opcode(nm_fd, &lookup_opcode, &lookup_resp) != 0) {
        close(nm_fd);
        return 0;
    }
    close(nm_fd);
    
    if (lookup_opcode == OP_ERROR) {
        display_error(lookup_resp);
        free(lookup_resp);
        return 0;
    }
    
    char ss_host[256] = {0};
    unsigned long ss_port_ul = 0;
    if (lookup_resp != NULL) {
        json_get_string(lookup_resp, "ss_ip", ss_host, sizeof(ss_host));
        json_get_uint(lookup_resp, "ss_port", &ss_port_ul);
        free(lookup_resp);
    }
    
    if (ss_port_ul == 0) {
        fprintf(stderr, "%s⚠️  Invalid storage server information%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    /* Connect to SS and request stream */
    int ss_fd = tcp_connect(ss_host, (uint16_t)ss_port_ul);
    if (ss_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Storage Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    char stream_payload[512];
    snprintf(stream_payload, sizeof(stream_payload),
             "{\"filename\":\"%s\",\"username\":\"%s\"}",
             filename, ctx->username);
    
    if (send_message(ss_fd, OP_STREAM_REQUEST, 0, stream_payload) != 0) {
        close(ss_fd);
        return 0;
    }
    
    printf("\n📖 Streaming '%s':\n", filename);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    /* Receive word chunks */
    while (1) {
        uint16_t opcode = 0;
        char *chunk = NULL;
        if (recv_with_opcode(ss_fd, &opcode, &chunk) != 0) {
            break;
        }
        
        if (opcode == OP_ERROR) {
            printf("\n");
            display_error(chunk);
            free(chunk);
            break;
        }
        
        if (opcode == OP_DATA_RESPONSE) {
            /* End of stream */
            free(chunk);
            break;
        }
        
        if (opcode == OP_DATA_CHUNK && chunk != NULL) {
            char word[512] = {0};
            json_get_string(chunk, "word", word, sizeof(word));
            printf("%s ", word);
            fflush(stdout);
        }
        
        free(chunk);
    }
    
    printf("\n%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n", COLOR_GREEN, COLOR_RESET);
    printf("%s✅ Stream complete%s\n", COLOR_GREEN, COLOR_RESET);
    
    close(ss_fd);
    (void)ctx;
    return 0;
}

int cmd_list(client_context_t *ctx, int argc, char **argv) {
    /* Connect to NM to get user list */
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    char payload[256];
    snprintf(payload, sizeof(payload), "{\"command\":\"LIST\"}");
    
    if (send_message(nm_fd, OP_COMMAND_FORWARD, 0, payload) != 0) {
        close(nm_fd);
        return 0;
    }
    
    uint16_t opcode = 0;
    char *resp = NULL;
    if (recv_with_opcode(nm_fd, &opcode, &resp) != 0) {
        close(nm_fd);
        return 0;
    }
    close(nm_fd);
    
    if (opcode == OP_ERROR) {
        display_error(resp);
        free(resp);
        return 0;
    }
    
    /* Display user list */
    printf("\n%s📋 Registered users:%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s\n", resp ? resp : "(no users)");
    
    free(resp);
    (void)argc;
    (void)argv;
    return 0;
}

int cmd_addaccess(client_context_t *ctx, int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: ADDACCESS -R|-W <filename> <username>\n");
        return 0;
    }
    
    const char *mode = argv[1];
    const char *filename = argv[2];
    const char *target_user = argv[3];
    
    int is_write = (strcmp(mode, "-W") == 0);
    
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"filename\":\"%s\",\"username\":\"%s\",\"command\":\"ADDACCESS\",\"target_user\":\"%s\",\"mode\":\"%s\"}",
             filename, ctx->username, target_user, is_write ? "write" : "read");
    
    if (send_message(nm_fd, OP_COMMAND_FORWARD, 5, payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to send ADDACCESS command%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    
    uint16_t opcode;
    char *resp_payload = NULL;
    if (recv_message(nm_fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to receive response%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    close(nm_fd);
    
    if (opcode == OP_ERROR) {
        display_error(resp_payload);
        free(resp_payload);
        return 0;
    }
    
    printf("%s✅ Access granted to '%s' for file '%s'%s\n", COLOR_GREEN, target_user, filename, COLOR_RESET);
    free(resp_payload);
    return 0;
}

int cmd_remaccess(client_context_t *ctx, int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: REMACCESS <filename> <username>\n");
        return 0;
    }
    
    const char *filename = argv[1];
    const char *target_user = argv[2];
    
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"filename\":\"%s\",\"username\":\"%s\",\"command\":\"REMACCESS\",\"target_user\":\"%s\"}",
             filename, ctx->username, target_user);
    
    if (send_message(nm_fd, OP_COMMAND_FORWARD, 6, payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to send REMACCESS command%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    
    uint16_t opcode;
    char *resp_payload = NULL;
    if (recv_message(nm_fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to receive response%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    close(nm_fd);
    
    if (opcode == OP_ERROR) {
        display_error(resp_payload);
        free(resp_payload);
        return 0;
    }
    
    printf("%s✅ Access removed from '%s' for file '%s'%s\n", COLOR_GREEN, target_user, filename, COLOR_RESET);
    free(resp_payload);
    return 0;
}

int cmd_requestaccess(client_context_t *ctx, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: REQUESTACCESS <filename> [read|write]\n");
        return 0;
    }
    const char *filename = argv[1];
    char mode[16];
    if (parse_mode_argument(argc >= 3 ? argv[2] : NULL, "read", mode, sizeof(mode)) != 0) {
        fprintf(stderr, "Mode must be 'read' or 'write'\n");
        return 0;
    }
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"filename\":\"%s\",\"username\":\"%s\",\"command\":\"REQUESTACCESS\",\"mode\":\"%s\"}",
             filename, ctx->username, mode);
    if (send_message(nm_fd, OP_COMMAND_FORWARD, 10, payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to send REQUESTACCESS command%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    uint16_t opcode;
    char *resp_payload = NULL;
    if (recv_message(nm_fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to receive response%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    close(nm_fd);
    if (opcode == OP_ERROR) {
        display_error(resp_payload);
        free(resp_payload);
        return 0;
    }
    printf("%s📨 Access request for '%s' (%s) submitted%s\n", COLOR_CYAN, filename, mode, COLOR_RESET);
    free(resp_payload);
    return 0;
}

int cmd_listrequests(client_context_t *ctx, int argc, char **argv) {
    const char *filename = (argc >= 2) ? argv[1] : NULL;
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    char payload[512];
    if (filename && filename[0] != '\0') {
        snprintf(payload, sizeof(payload),
                 "{\"filename\":\"%s\",\"username\":\"%s\",\"command\":\"LISTREQUESTS\"}",
                 filename, ctx->username);
    } else {
        snprintf(payload, sizeof(payload),
                 "{\"username\":\"%s\",\"command\":\"LISTREQUESTS\"}",
                 ctx->username);
    }
    if (send_message(nm_fd, OP_COMMAND_FORWARD, 11, payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to send LISTREQUESTS command%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    uint16_t opcode;
    char *resp_payload = NULL;
    if (recv_message(nm_fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to receive response%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    close(nm_fd);
    if (opcode == OP_ERROR) {
        display_error(resp_payload);
        free(resp_payload);
        return 0;
    }
    printf("\n%s📬 Pending requests%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s\n", resp_payload ? resp_payload : "(none)");
    free(resp_payload);
    return 0;
}

int cmd_approverequest(client_context_t *ctx, int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: APPROVEREQUEST <filename> <username> <read|write>\n");
        return 0;
    }
    const char *filename = argv[1];
    const char *target_user = argv[2];
    char mode[16];
    if (parse_mode_argument(argv[3], "read", mode, sizeof(mode)) != 0) {
        fprintf(stderr, "Mode must be 'read' or 'write'\n");
        return 0;
    }
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"filename\":\"%s\",\"username\":\"%s\",\"command\":\"APPROVEREQUEST\",\"target_user\":\"%s\",\"mode\":\"%s\"}",
             filename, ctx->username, target_user, mode);
    if (send_message(nm_fd, OP_COMMAND_FORWARD, 12, payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to send APPROVEREQUEST command%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    uint16_t opcode;
    char *resp_payload = NULL;
    if (recv_message(nm_fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to receive response%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    close(nm_fd);
    if (opcode == OP_ERROR) {
        display_error(resp_payload);
        free(resp_payload);
        return 0;
    }
    printf("%s✅ Approved %s access for '%s'%s\n", COLOR_GREEN, mode, target_user, COLOR_RESET);
    free(resp_payload);
    return 0;
}

int cmd_denyrequest(client_context_t *ctx, int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: DENYREQUEST <filename> <username> <read|write>\n");
        return 0;
    }
    const char *filename = argv[1];
    const char *target_user = argv[2];
    char mode[16];
    if (parse_mode_argument(argv[3], "read", mode, sizeof(mode)) != 0) {
        fprintf(stderr, "Mode must be 'read' or 'write'\n");
        return 0;
    }
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"filename\":\"%s\",\"username\":\"%s\",\"command\":\"DENYREQUEST\",\"target_user\":\"%s\",\"mode\":\"%s\"}",
             filename, ctx->username, target_user, mode);
    if (send_message(nm_fd, OP_COMMAND_FORWARD, 13, payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to send DENYREQUEST command%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    uint16_t opcode;
    char *resp_payload = NULL;
    if (recv_message(nm_fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to receive response%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    close(nm_fd);
    if (opcode == OP_ERROR) {
        display_error(resp_payload);
        free(resp_payload);
        return 0;
    }
    printf("%s⚖️  Denied %s request from '%s'%s\n", COLOR_YELLOW, mode, target_user, COLOR_RESET);
    free(resp_payload);
    return 0;
}

int cmd_checkpoint(client_context_t *ctx, int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: CHECKPOINT <filename> <tag>\n");
        return 0;
    }
    const char *filename = argv[1];
    const char *tag = argv[2];

    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }

    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"filename\":\"%s\",\"tag\":\"%s\",\"username\":\"%s\",\"command\":\"CHECKPOINT\"}",
             filename, tag, ctx->username);

    if (send_message(nm_fd, OP_COMMAND_FORWARD, 14, payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to send CHECKPOINT command%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }

    uint16_t opcode;
    char *resp_payload = NULL;
    if (recv_message(nm_fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to receive response%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    close(nm_fd);

    if (opcode == OP_ERROR) {
        display_error(resp_payload);
        free(resp_payload);
        return 0;
    }

    printf("%s💾 Created checkpoint '%s' for '%s'%s\n", COLOR_GREEN, tag, filename, COLOR_RESET);
    free(resp_payload);
    return 0;
}

int cmd_listcheckpoints(client_context_t *ctx, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: LISTCHECKPOINTS <filename>\n");
        return 0;
    }
    const char *filename = argv[1];

    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }

    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"filename\":\"%s\",\"username\":\"%s\",\"command\":\"LISTCHECKPOINTS\"}",
             filename, ctx->username);

    if (send_message(nm_fd, OP_COMMAND_FORWARD, 15, payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to send LISTCHECKPOINTS command%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }

    uint16_t opcode;
    char *resp_payload = NULL;
    if (recv_message(nm_fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to receive response%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    close(nm_fd);

    if (opcode == OP_ERROR) {
        display_error(resp_payload);
        free(resp_payload);
        return 0;
    }

    printf("\n%s📚 Checkpoints for '%s'%s\n", COLOR_CYAN, filename, COLOR_RESET);
    printf("%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n", COLOR_CYAN, COLOR_RESET);
    int printed = 0;
    if (resp_payload != NULL) {
        const char *list = strstr(resp_payload, "\"checkpoints\":[");
        if (list != NULL) {
            list += strlen("\"checkpoints\":[");
            const char *ptr = list;
            while (ptr && *ptr && *ptr != ']') {
                if (*ptr == '"') {
                    ptr++;
                    const char *start = ptr;
                    while (*ptr && *ptr != '"') ptr++;
                    if (*ptr == '"') {
                        size_t len = (size_t)(ptr - start);
                        if (len > 0) {
                            char tag[256];
                            if (len >= sizeof(tag)) len = sizeof(tag) - 1;
                            memcpy(tag, start, len);
                            tag[len] = '\0';
                            printf("  %s•%s %s\n", COLOR_BLUE, COLOR_RESET, tag);
                            printed++;
                        }
                    }
                }
                ptr++;
            }
        }
    }
    if (printed == 0) {
        printf("  %s(no checkpoints)%s\n", COLOR_YELLOW, COLOR_RESET);
    }
    printf("%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n", COLOR_CYAN, COLOR_RESET);
    free(resp_payload);
    return 0;
}

int cmd_viewcheckpoint(client_context_t *ctx, int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: VIEWCHECKPOINT <filename> <tag>\n");
        return 0;
    }
    const char *filename = argv[1];
    const char *tag = argv[2];

    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }

    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"filename\":\"%s\",\"tag\":\"%s\",\"username\":\"%s\",\"command\":\"VIEWCHECKPOINT\"}",
             filename, tag, ctx->username);

    if (send_message(nm_fd, OP_COMMAND_FORWARD, 16, payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to send VIEWCHECKPOINT command%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }

    uint16_t opcode;
    char *resp_payload = NULL;
    if (recv_message(nm_fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to receive response%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    close(nm_fd);

    if (opcode == OP_ERROR) {
        display_error(resp_payload);
        free(resp_payload);
        return 0;
    }

    char content[4096];
    int has_content = (resp_payload != NULL && json_get_string(resp_payload, "content", content, sizeof(content)) == 0);
    printf("\n%s🧾 Checkpoint '%s' for '%s'%s\n", COLOR_CYAN, tag, filename, COLOR_RESET);
    printf("%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n", COLOR_CYAN, COLOR_RESET);
    if (has_content) {
        printf("%s\n", content);
    } else if (resp_payload != NULL) {
        printf("%s\n", resp_payload);
    } else {
        printf("(empty checkpoint)\n");
    }
    printf("%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n", COLOR_CYAN, COLOR_RESET);
    free(resp_payload);
    return 0;
}

int cmd_revertcheckpoint(client_context_t *ctx, int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: REVERTCHECKPOINT <filename> <tag>\n");
        return 0;
    }
    const char *filename = argv[1];
    const char *tag = argv[2];

    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }

    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"filename\":\"%s\",\"tag\":\"%s\",\"username\":\"%s\",\"command\":\"REVERTCHECKPOINT\"}",
             filename, tag, ctx->username);

    if (send_message(nm_fd, OP_COMMAND_FORWARD, 17, payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to send REVERTCHECKPOINT command%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return  0;
    }

    uint16_t opcode;
    char *resp_payload = NULL;
    if (recv_message(nm_fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to receive response%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    close(nm_fd);

    if (opcode == OP_ERROR) {
        display_error(resp_payload);
        free(resp_payload);
        return 0;
    }

    printf("%s⏪ Reverted '%s' to checkpoint '%s'%s\n", COLOR_GREEN, filename, tag, COLOR_RESET);
    free(resp_payload);
    return 0;
}

int cmd_exec(client_context_t *ctx, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: EXEC <filename>\n");
        return 0;
    }
    const char *filename = argv[1];
    
    /* Connect to NM to execute file */
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"command\":\"EXEC\",\"filename\":\"%s\",\"username\":\"%s\"}",
             filename, ctx->username);
    
    if (send_message(nm_fd, OP_COMMAND_FORWARD, 0, payload) != 0) {
        close(nm_fd);
        return 0;
    }
    
    /* Receive execution output from NM */
    uint16_t opcode = 0;
    char *resp = NULL;
    if (recv_with_opcode(nm_fd, &opcode, &resp) != 0) {
        close(nm_fd);
        return 0;
    }
    close(nm_fd);
    
    if (opcode == OP_ERROR) {
        display_error(resp);
        free(resp);
        return 0;
    }
    
    /* Display execution output */
    printf("\n%s📜 Execution output:%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s\n", resp ? resp : "(no output)");
    printf("%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n", COLOR_CYAN, COLOR_RESET);
    
    free(resp);
    return 0;
}

int cmd_createfolder(client_context_t *ctx, int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: CREATEFOLDER <foldername>\n");
        return 0;
    }
    
    const char *foldername = argv[1];
    
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"foldername\":\"%s\",\"username\":\"%s\",\"command\":\"CREATEFOLDER\"}",
             foldername, ctx->username);
    
    if (send_message(nm_fd, OP_CREATEFOLDER_REQUEST, 7, payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to send CREATEFOLDER command%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    
    uint16_t opcode;
    char *resp_payload = NULL;
    if (recv_message(nm_fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to receive response%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    close(nm_fd);
    
    if (opcode == OP_ERROR) {
        display_error(resp_payload);
        free(resp_payload);
        return 0;
    }
    
    printf("%s✅ Folder '%s' created successfully%s\n", COLOR_GREEN, foldername, COLOR_RESET);
    free(resp_payload);
    return 0;
}

int cmd_move(client_context_t *ctx, int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: MOVE <filename> <foldername>\n");
        return 0;
    }
    
    const char *filename = argv[1];
    const char *foldername = argv[2];
    
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"filename\":\"%s\",\"foldername\":\"%s\",\"username\":\"%s\",\"command\":\"MOVE\"}",
             filename, foldername, ctx->username);
    
    if (send_message(nm_fd, OP_MOVE_REQUEST, 8, payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to send MOVE command%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    
    uint16_t opcode;
    char *resp_payload = NULL;
    if (recv_message(nm_fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to receive response%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    close(nm_fd);
    
    if (opcode == OP_ERROR) {
        display_error(resp_payload);
        free(resp_payload);
        return 0;
    }
    
    printf("%s✅ File '%s' moved to folder '%s'%s\n", COLOR_GREEN, filename, foldername, COLOR_RESET);
    free(resp_payload);
    return 0;
}

int cmd_viewfolder(client_context_t *ctx, int argc, char **argv) {
    const char *foldername = (argc >= 2) ? argv[1] : "/";
    
    int nm_fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (nm_fd < 0) {
        fprintf(stderr, "%s⚠️  Failed to connect to Name Server%s\n", COLOR_YELLOW, COLOR_RESET);
        return 0;
    }
    
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"foldername\":\"%s\",\"username\":\"%s\",\"command\":\"VIEWFOLDER\"}",
             foldername, ctx->username);
    
    if (send_message(nm_fd, OP_VIEWFOLDER_REQUEST, 9, payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to send VIEWFOLDER command%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    
    uint16_t opcode;
    char *resp_payload = NULL;
    if (recv_message(nm_fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s⚠️  Failed to receive response%s\n", COLOR_YELLOW, COLOR_RESET);
        close(nm_fd);
        return 0;
    }
    close(nm_fd);
    
    if (opcode == OP_ERROR) {
        display_error(resp_payload);
        free(resp_payload);
        return 0;
    }
    
    printf("\n%s📁 Contents of folder: %s%s\n", COLOR_CYAN, foldername, COLOR_RESET);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    size_t folder_count = print_viewfolder_section(resp_payload, "folders", "Folders", COLOR_YELLOW, "📁");
    size_t file_count = print_viewfolder_section(resp_payload, "files", "Files", COLOR_BLUE, "📄");

    if (folder_count == 0 && file_count == 0) {
        printf("  %s(empty)%s\n", COLOR_YELLOW, COLOR_RESET);
    }

    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    free(resp_payload);
    return 0;
}

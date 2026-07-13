#include "protocol.h"
#include "persistence.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ASSERT_EQ(actual, expected, msg) \
    do { \
        if ((actual) != (expected)) { \
            fprintf(stderr, "%s:%d: %s (got %d expected %d)\n", __FILE__, __LINE__, msg, (int)(actual), (int)(expected)); \
            return 1; \
        } \
    } while (0)

#define ASSERT_STR_EQ(actual, expected, msg) \
    do { \
        if (strcmp((actual), (expected)) != 0) { \
            fprintf(stderr, "%s:%d: %s (got '%s' expected '%s')\n", __FILE__, __LINE__, msg, (actual), (expected)); \
            return 1; \
        } \
    } while (0)

static int test_header_roundtrip(void) {
    message_header_t header = {
        .version = PROTOCOL_VERSION,
        .opcode = OP_LOOKUP_RESP,
        .request_id = 0xAABBCCDDu,
        .payload_len = 4096u
    };

    uint8_t buffer[PROTOCOL_HEADER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    ASSERT_EQ(protocol_encode_header(&header, buffer), 0, "encode header should succeed");

    message_header_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    ASSERT_EQ(protocol_decode_header(buffer, &decoded), 0, "decode header should succeed");

    ASSERT_EQ(decoded.version, header.version, "version mismatch");
    ASSERT_EQ(decoded.opcode, header.opcode, "opcode mismatch");
    ASSERT_EQ(decoded.request_id, header.request_id, "request id mismatch");
    ASSERT_EQ(decoded.payload_len, header.payload_len, "payload len mismatch");

    ASSERT_EQ(protocol_encode_header(NULL, buffer), -1, "encode should fail on null header");
    ASSERT_EQ(protocol_encode_header(&header, NULL), -1, "encode should fail on null buffer");
    ASSERT_EQ(protocol_decode_header(NULL, &decoded), -1, "decode should fail on null input");
    ASSERT_EQ(protocol_decode_header(buffer, NULL), -1, "decode should fail on null output");
    return 0;
}

static int test_name_tables(void) {
    ASSERT_STR_EQ(protocol_opcode_name(OP_STOP), "OP_STOP", "opcode name should match");
    ASSERT_STR_EQ(protocol_error_name(ERR_STORAGE_DOWN), "ERR_STORAGE_DOWN", "error name should match");
    ASSERT_STR_EQ(protocol_opcode_name(0x1234), "OP_UNKNOWN", "unknown opcode should map to placeholder");
    ASSERT_STR_EQ(protocol_error_name(999), "ERR_UNKNOWN", "unknown error should map to placeholder");
    return 0;
}

static int test_paths(void) {
    char buf[256];
    ASSERT_EQ(ns_registry_path(buf, sizeof(buf), "/var/lib/nm"), 0, "ns registry path");
    ASSERT_STR_EQ(buf, "/var/lib/nm/registry.jsonl", "ns registry path value");

    ASSERT_EQ(ns_acl_path(buf, sizeof(buf), "/var/lib/nm", "foo.txt"), 0, "ns acl path");
    ASSERT_STR_EQ(buf, "/var/lib/nm/acl/foo.txt.acl.json", "ns acl path value");

    time_t fixed = 1762732800; // 2025-11-10 00:00:00 UTC
    ASSERT_EQ(nm_log_path(buf, sizeof(buf), "/var/lib/nm", fixed), 0, "nm log path");
    ASSERT_STR_EQ(buf, "/var/lib/nm/logs/nm_20251110.log", "nm log path value");

    ASSERT_EQ(ss_file_path(buf, sizeof(buf), "/mnt/data", "abcd", "foo.txt"), 0, "ss file path");
    ASSERT_STR_EQ(buf, "/mnt/data/ss_data_abcd/files/foo.txt", "ss file path value");

    ASSERT_EQ(ss_metadata_path(buf, sizeof(buf), "/mnt/data", "abcd", "foo.txt"), 0, "ss metadata path");
    ASSERT_STR_EQ(buf, "/mnt/data/ss_data_abcd/metadata/foo.txt.meta.json", "ss metadata path value");

    ASSERT_EQ(ss_undo_path(buf, sizeof(buf), "/mnt/data", "abcd", "foo.txt"), 0, "ss undo path");
    ASSERT_STR_EQ(buf, "/mnt/data/ss_data_abcd/undo/foo.txt.undo.json", "ss undo path value");

    ASSERT_EQ(ss_log_path(buf, sizeof(buf), "/mnt/data", "abcd", fixed), 0, "ss log path");
    ASSERT_STR_EQ(buf, "/mnt/data/ss_data_abcd/logs/ss_20251110.log", "ss log path value");

    ASSERT_EQ(ns_registry_path(NULL, sizeof(buf), "/tmp"), -1, "null buffer should fail");
    ASSERT_EQ(ns_registry_path(buf, 0, "/tmp"), -1, "zero length should fail");
    ASSERT_EQ(ns_registry_path(buf, sizeof(buf), NULL), -1, "null base should fail");
    return 0;
}

int main(void) {
    if (test_header_roundtrip() != 0) {
        return EXIT_FAILURE;
    }
    if (test_name_tables() != 0) {
        return EXIT_FAILURE;
    }
    if (test_paths() != 0) {
        return EXIT_FAILURE;
    }
    puts("All Phase 0 protocol tests passed.");
    return EXIT_SUCCESS;
}

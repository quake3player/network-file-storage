#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define PROTOCOL_VERSION 0x0001u
#define PROTOCOL_HEADER_SIZE 12u

#define PROTOCOL_HEARTBEAT_INTERVAL_MS 5000u
#define PROTOCOL_HEARTBEAT_TIMEOUT_MS 15000u
#define PROTOCOL_STREAM_DELAY_MS 100u
#define PROTOCOL_RETRY_LIMIT 3u

typedef struct __attribute__((packed)) {
    uint16_t version;
    uint16_t opcode;
    uint32_t request_id;
    uint32_t payload_len;
} message_header_t;

typedef enum {
    OP_REGISTER_SS = 0x0001,
    OP_REGISTER_CLIENT = 0x0002,
    OP_REGISTER_ACK = 0x8001,
    OP_HEARTBEAT = 0x0003,
    OP_LOOKUP_FILE = 0x0004,
    OP_LOOKUP_RESP = 0x8004,
    OP_COMMAND_FORWARD = 0x0005,
    OP_COMMAND_STATUS = 0x8005,
    OP_DATA_REQUEST = 0x0006,
    OP_DATA_RESPONSE = 0x8007,
    OP_DATA_CHUNK = 0x8006,
    OP_DATA_ACK = 0x0007,
    OP_WRITE_REQUEST = 0x0008,
    OP_WRITE_UPDATE = 0x0009,
    OP_WRITE_COMMIT = 0x000A,
    OP_WRITE_ACK = 0x800A,
    OP_STREAM_REQUEST = 0x000B,
    OP_INFO_REQUEST = 0x000C,
    OP_UNDO_REQUEST = 0x000D,
    OP_DELETE_REQUEST = 0x000E,
    OP_CREATEFOLDER_REQUEST = 0x000F,
    OP_MOVE_REQUEST = 0x0010,
    OP_VIEWFOLDER_REQUEST = 0x0011,
    OP_VIEWFOLDER_RESPONSE = 0x8011,
    OP_CHECKPOINT_REQUEST = 0x0012,
    OP_VIEWCHECKPOINT_REQUEST = 0x0013,
    OP_REVERT_CHECKPOINT_REQUEST = 0x0014,
    OP_LISTCHECKPOINTS_REQUEST = 0x0015,
    OP_LISTCHECKPOINTS_RESPONSE = 0x8015,
    OP_REPLICATE_FILE = 0x0016,
    OP_LIST_FOLDERS_REQUEST = 0x0017,
    OP_LIST_FOLDERS_RESPONSE = 0x8017,
    OP_STOP = 0x7FFF,
    OP_ERROR = 0xFFFF
} protocol_opcode_t;

typedef enum {
    ERR_OK = 0,
    ERR_NO_ACCESS,
    ERR_FILE_NOT_FOUND,
    ERR_SENTENCE_LOCKED,
    ERR_INVALID_SENTENCE,
    ERR_INVALID_WORD,
    ERR_DUPLICATE,
    ERR_NOT_OWNER,
    ERR_UNREGISTERED_USER,
    ERR_STORAGE_DOWN,
    ERR_UNDO_EMPTY,
    ERR_EXECUTION_FAIL,
    ERR_PROTOCOL,
    ERR_FOLDER_EXISTS,
    ERR_NOT_A_FOLDER,
    ERR_FOLDER_NOT_EMPTY,
    ERR_CHECKPOINT_EXISTS,
    ERR_CHECKPOINT_NOT_FOUND,
    ERR_REQUEST_EXISTS,
    ERR_NO_REQUEST
} protocol_error_t;

int protocol_encode_header(const message_header_t *header, uint8_t out[PROTOCOL_HEADER_SIZE]);
int protocol_decode_header(const uint8_t in[PROTOCOL_HEADER_SIZE], message_header_t *out_header);
const char *protocol_opcode_name(protocol_opcode_t opcode);
const char *protocol_error_name(protocol_error_t code);

#endif // PROTOCOL_H

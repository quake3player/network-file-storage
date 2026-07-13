#include "protocol.h"

#include <arpa/inet.h>
#include <string.h>

int protocol_encode_header(const message_header_t *header, uint8_t out[PROTOCOL_HEADER_SIZE]) {
    if (header == NULL || out == NULL) {
        return -1;
    }

    const uint16_t version = htons(header->version);
    const uint16_t opcode = htons(header->opcode);
    const uint32_t request_id = htonl(header->request_id);
    const uint32_t payload_len = htonl(header->payload_len);

    memcpy(out + 0, &version, sizeof(version));
    memcpy(out + 2, &opcode, sizeof(opcode));
    memcpy(out + 4, &request_id, sizeof(request_id));
    memcpy(out + 8, &payload_len, sizeof(payload_len));
    return 0;
}

int protocol_decode_header(const uint8_t in[PROTOCOL_HEADER_SIZE], message_header_t *out_header) {
    if (in == NULL || out_header == NULL) {
        return -1;
    }

    uint16_t version = 0;
    uint16_t opcode = 0;
    uint32_t request_id = 0;
    uint32_t payload_len = 0;

    memcpy(&version, in + 0, sizeof(version));
    memcpy(&opcode, in + 2, sizeof(opcode));
    memcpy(&request_id, in + 4, sizeof(request_id));
    memcpy(&payload_len, in + 8, sizeof(payload_len));

    out_header->version = ntohs(version);
    out_header->opcode = ntohs(opcode);
    out_header->request_id = ntohl(request_id);
    out_header->payload_len = ntohl(payload_len);

    return 0;
}

const char *protocol_opcode_name(protocol_opcode_t opcode) {
    switch (opcode) {
        case OP_REGISTER_SS:
            return "OP_REGISTER_SS";
        case OP_REGISTER_CLIENT:
            return "OP_REGISTER_CLIENT";
        case OP_REGISTER_ACK:
            return "OP_REGISTER_ACK";
        case OP_HEARTBEAT:
            return "OP_HEARTBEAT";
        case OP_LOOKUP_FILE:
            return "OP_LOOKUP_FILE";
        case OP_LOOKUP_RESP:
            return "OP_LOOKUP_RESP";
        case OP_COMMAND_FORWARD:
            return "OP_COMMAND_FORWARD";
        case OP_COMMAND_STATUS:
            return "OP_COMMAND_STATUS";
        case OP_DATA_REQUEST:
            return "OP_DATA_REQUEST";
        case OP_DATA_RESPONSE:
            return "OP_DATA_RESPONSE";
        case OP_DATA_CHUNK:
            return "OP_DATA_CHUNK";
        case OP_DATA_ACK:
            return "OP_DATA_ACK";
        case OP_WRITE_REQUEST:
            return "OP_WRITE_REQUEST";
        case OP_WRITE_UPDATE:
            return "OP_WRITE_UPDATE";
        case OP_WRITE_COMMIT:
            return "OP_WRITE_COMMIT";
        case OP_WRITE_ACK:
            return "OP_WRITE_ACK";
        case OP_STREAM_REQUEST:
            return "OP_STREAM_REQUEST";
        case OP_INFO_REQUEST:
            return "OP_INFO_REQUEST";
        case OP_UNDO_REQUEST:
            return "OP_UNDO_REQUEST";
        case OP_DELETE_REQUEST:
            return "OP_DELETE_REQUEST";
        case OP_CREATEFOLDER_REQUEST:
            return "OP_CREATEFOLDER_REQUEST";
        case OP_MOVE_REQUEST:
            return "OP_MOVE_REQUEST";
        case OP_VIEWFOLDER_REQUEST:
            return "OP_VIEWFOLDER_REQUEST";
        case OP_VIEWFOLDER_RESPONSE:
            return "OP_VIEWFOLDER_RESPONSE";
        case OP_CHECKPOINT_REQUEST:
            return "OP_CHECKPOINT_REQUEST";
        case OP_VIEWCHECKPOINT_REQUEST:
            return "OP_VIEWCHECKPOINT_REQUEST";
        case OP_REVERT_CHECKPOINT_REQUEST:
            return "OP_REVERT_CHECKPOINT_REQUEST";
        case OP_LISTCHECKPOINTS_REQUEST:
            return "OP_LISTCHECKPOINTS_REQUEST";
        case OP_LISTCHECKPOINTS_RESPONSE:
            return "OP_LISTCHECKPOINTS_RESPONSE";
        case OP_REPLICATE_FILE:
            return "OP_REPLICATE_FILE";
        case OP_LIST_FOLDERS_REQUEST:
            return "OP_LIST_FOLDERS_REQUEST";
        case OP_LIST_FOLDERS_RESPONSE:
            return "OP_LIST_FOLDERS_RESPONSE";
        case OP_STOP:
            return "OP_STOP";
        case OP_ERROR:
            return "OP_ERROR";
    }
    return "OP_UNKNOWN";
}

const char *protocol_error_name(protocol_error_t code) {
    switch (code) {
        case ERR_OK:
            return "ERR_OK";
        case ERR_NO_ACCESS:
            return "ERR_NO_ACCESS";
        case ERR_FILE_NOT_FOUND:
            return "ERR_FILE_NOT_FOUND";
        case ERR_SENTENCE_LOCKED:
            return "ERR_SENTENCE_LOCKED";
        case ERR_INVALID_SENTENCE:
            return "ERR_INVALID_SENTENCE";
        case ERR_INVALID_WORD:
            return "ERR_INVALID_WORD";
        case ERR_DUPLICATE:
            return "ERR_DUPLICATE";
        case ERR_NOT_OWNER:
            return "ERR_NOT_OWNER";
        case ERR_UNREGISTERED_USER:
            return "ERR_UNREGISTERED_USER";
        case ERR_STORAGE_DOWN:
            return "ERR_STORAGE_DOWN";
        case ERR_UNDO_EMPTY:
            return "ERR_UNDO_EMPTY";
        case ERR_EXECUTION_FAIL:
            return "ERR_EXECUTION_FAIL";
        case ERR_PROTOCOL:
            return "ERR_PROTOCOL";
        case ERR_FOLDER_EXISTS:
            return "ERR_FOLDER_EXISTS";
        case ERR_NOT_A_FOLDER:
            return "ERR_NOT_A_FOLDER";
        case ERR_FOLDER_NOT_EMPTY:
            return "ERR_FOLDER_NOT_EMPTY";
        case ERR_CHECKPOINT_EXISTS:
            return "ERR_CHECKPOINT_EXISTS";
        case ERR_CHECKPOINT_NOT_FOUND:
            return "ERR_CHECKPOINT_NOT_FOUND";
        case ERR_REQUEST_EXISTS:
            return "ERR_REQUEST_EXISTS";
        case ERR_NO_REQUEST:
            return "ERR_NO_REQUEST";
    }
    return "ERR_UNKNOWN";
}

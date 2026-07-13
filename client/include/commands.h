#ifndef COMMANDS_H
#define COMMANDS_H

#include <stddef.h>
#include <stdint.h>

typedef struct client_context {
    char nm_host[256];
    uint16_t nm_port;
    char username[128];
    uint16_t client_port;
    char session_token[64];
} client_context_t;

int cmd_view(client_context_t *ctx, int argc, char **argv);
int cmd_read(client_context_t *ctx, int argc, char **argv);
int cmd_create(client_context_t *ctx, int argc, char **argv);
int cmd_write(client_context_t *ctx, int argc, char **argv);
int cmd_delete(client_context_t *ctx, int argc, char **argv);
int cmd_undo(client_context_t *ctx, int argc, char **argv);
int cmd_info(client_context_t *ctx, int argc, char **argv);
int cmd_stream(client_context_t *ctx, int argc, char **argv);
int cmd_list(client_context_t *ctx, int argc, char **argv);
int cmd_addaccess(client_context_t *ctx, int argc, char **argv);
int cmd_remaccess(client_context_t *ctx, int argc, char **argv);
int cmd_requestaccess(client_context_t *ctx, int argc, char **argv);
int cmd_listrequests(client_context_t *ctx, int argc, char **argv);
int cmd_approverequest(client_context_t *ctx, int argc, char **argv);
int cmd_denyrequest(client_context_t *ctx, int argc, char **argv);
int cmd_checkpoint(client_context_t *ctx, int argc, char **argv);
int cmd_listcheckpoints(client_context_t *ctx, int argc, char **argv);
int cmd_viewcheckpoint(client_context_t *ctx, int argc, char **argv);
int cmd_revertcheckpoint(client_context_t *ctx, int argc, char **argv);
int cmd_exec(client_context_t *ctx, int argc, char **argv);
int cmd_createfolder(client_context_t *ctx, int argc, char **argv);
int cmd_move(client_context_t *ctx, int argc, char **argv);
int cmd_viewfolder(client_context_t *ctx, int argc, char **argv);

int send_message(int fd, uint16_t opcode, uint32_t request_id, const char *payload);
int recv_message(int fd, uint16_t *opcode_out, uint32_t *request_id_out, char **payload_out);

#endif // COMMANDS_H

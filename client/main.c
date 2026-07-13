#define _POSIX_C_SOURCE 200809L

#include "commands.h"
#include "net.h"
#include "protocol.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ANSI Color codes */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <nm-host> <nm-port>\n", prog);
    fprintf(stderr, "Interactive client shell for Docs++ system\n");
}

static int register_client(client_context_t *ctx) {
    int fd = tcp_connect(ctx->nm_host, ctx->nm_port);
    if (fd < 0) {
        fprintf(stderr, "%s❌ Failed to connect to Name Server at %s:%u%s\n", COLOR_RED, ctx->nm_host, ctx->nm_port, COLOR_RESET);
        return -1;
    }

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"username\":\"%s\",\"client_ip\":\"0.0.0.0\",\"client_port\":%u}",
             ctx->username, ctx->client_port);

    if (send_message(fd, OP_REGISTER_CLIENT, 1u, payload) != 0) {
        fprintf(stderr, "%s❌ Failed to send registration%s\n", COLOR_RED, COLOR_RESET);
        close(fd);
        return -1;
    }

    uint16_t opcode;
    char *resp_payload = NULL;
    if (recv_message(fd, &opcode, NULL, &resp_payload) != 0) {
        fprintf(stderr, "%s❌ Failed to receive registration response%s\n", COLOR_RED, COLOR_RESET);
        close(fd);
        return -1;
    }

    if (opcode == OP_REGISTER_ACK) {
        printf("%s✓ Connected to Name Server as '%s'%s\n", COLOR_GREEN, ctx->username, COLOR_RESET);
        free(resp_payload);
        close(fd);
        return 0;
    } else if (opcode == OP_ERROR) {
        fprintf(stderr, "%s❌ Registration error: %s%s\n", COLOR_RED, resp_payload ? resp_payload : "Unknown", COLOR_RESET);
        free(resp_payload);
        close(fd);
        return -1;
    }

    fprintf(stderr, "%s❌ Unexpected response opcode: 0x%04x%s\n", COLOR_RED, opcode, COLOR_RESET);
    free(resp_payload);
    close(fd);
    return -1;
}

static int parse_and_execute(client_context_t *ctx, char *line) {
    char *argv[16];
    int argc = 0;
    
    char *token = strtok(line, " \t\n");
    while (token != NULL && argc < 16) {
        argv[argc++] = token;
        token = strtok(NULL, " \t\n");
    }
    
    if (argc == 0) {
        return 0;
    }
    
    const char *cmd = argv[0];
    
    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
        return -1;
    } else if (strcmp(cmd, "help") == 0) {
        printf("\n%sAvailable commands:%s\n", COLOR_BOLD, COLOR_RESET);
        printf("  %sFile Operations:%s\n", COLOR_CYAN, COLOR_RESET);
        printf("    VIEW [-a] [-l]                  List files\n");
        printf("    READ <file>                     Display file content\n");
        printf("    CREATE <file>                   Create new file\n");
        printf("    WRITE <file> <sent#>            Edit file (interactive)\n");
        printf("    DELETE <file>                   Delete file\n");
        printf("    UNDO <file>                     Undo last change\n");
        printf("    INFO <file>                     Show file metadata\n");
        printf("    STREAM <file>                   Stream content word-by-word\n");
        
        printf("\n  %sCheckpoints:%s\n", COLOR_CYAN, COLOR_RESET);
        printf("    CHECKPOINT <file> <tag>         Snapshot current file\n");
        printf("    LISTCHECKPOINTS <file>          List saved checkpoints\n");
        printf("    VIEWCHECKPOINT <file> <tag>     View checkpoint contents\n");
        printf("    REVERTCHECKPOINT <file> <tag>   Restore from checkpoint\n");
        
        printf("\n  %sAccess Control:%s\n", COLOR_CYAN, COLOR_RESET);
        printf("    LIST                            List all users\n");
        printf("    ADDACCESS -R|-W <file> <user>   Grant access\n");
        printf("    REMACCESS <file> <user>         Remove access\n");
        printf("    REQUESTACCESS <file> [mode]     Ask owner for access\n");
        printf("    LISTREQUESTS [file]             View pending requests\n");
        printf("    APPROVEREQUEST <file> <u...>    Approve a request\n");
        printf("    DENYREQUEST <file> <u...>       Deny a request\n");
        
        printf("\n  %sFolder Operations:%s\n", COLOR_CYAN, COLOR_RESET);
        printf("    CREATEFOLDER <folder>           Create a new folder\n");
        printf("    MOVE <file> <folder>            Move file to folder\n");
        printf("    VIEWFOLDER [folder]             List files in folder\n");
        
        printf("\n  %sSystem:%s\n", COLOR_CYAN, COLOR_RESET);
        printf("    EXEC <file>                     Execute script\n");
        printf("    exit / quit                     Exit shell\n");
        return 0;
    } else if (strcmp(cmd, "VIEW") == 0) {
        return cmd_view(ctx, argc, argv);
    } else if (strcmp(cmd, "READ") == 0) {
        return cmd_read(ctx, argc, argv);
    } else if (strcmp(cmd, "CREATE") == 0) {
        return cmd_create(ctx, argc, argv);
    } else if (strcmp(cmd, "WRITE") == 0) {
        return cmd_write(ctx, argc, argv);
    } else if (strcmp(cmd, "DELETE") == 0) {
        return cmd_delete(ctx, argc, argv);
    } else if (strcmp(cmd, "UNDO") == 0) {
        return cmd_undo(ctx, argc, argv);
    } else if (strcmp(cmd, "INFO") == 0) {
        return cmd_info(ctx, argc, argv);
    } else if (strcmp(cmd, "STREAM") == 0) {
        return cmd_stream(ctx, argc, argv);
    } else if (strcmp(cmd, "LIST") == 0) {
        return cmd_list(ctx, argc, argv);
    } else if (strcmp(cmd, "ADDACCESS") == 0) {
        return cmd_addaccess(ctx, argc, argv);
    } else if (strcmp(cmd, "REMACCESS") == 0) {
        return cmd_remaccess(ctx, argc, argv);
    } else if (strcmp(cmd, "REQUESTACCESS") == 0) {
        return cmd_requestaccess(ctx, argc, argv);
    } else if (strcmp(cmd, "LISTREQUESTS") == 0) {
        return cmd_listrequests(ctx, argc, argv);
    } else if (strcmp(cmd, "APPROVEREQUEST") == 0) {
        return cmd_approverequest(ctx, argc, argv);
    } else if (strcmp(cmd, "DENYREQUEST") == 0) {
        return cmd_denyrequest(ctx, argc, argv);
    } else if (strcmp(cmd, "CHECKPOINT") == 0) {
        return cmd_checkpoint(ctx, argc, argv);
    } else if (strcmp(cmd, "LISTCHECKPOINTS") == 0) {
        return cmd_listcheckpoints(ctx, argc, argv);
    } else if (strcmp(cmd, "VIEWCHECKPOINT") == 0) {
        return cmd_viewcheckpoint(ctx, argc, argv);
    } else if (strcmp(cmd, "REVERTCHECKPOINT") == 0) {
        return cmd_revertcheckpoint(ctx, argc, argv);
    } else if (strcmp(cmd, "EXEC") == 0) {
        return cmd_exec(ctx, argc, argv);
    } else if (strcmp(cmd, "CREATEFOLDER") == 0) {
        return cmd_createfolder(ctx, argc, argv);
    } else if (strcmp(cmd, "MOVE") == 0) {
        return cmd_move(ctx, argc, argv);
    } else if (strcmp(cmd, "VIEWFOLDER") == 0) {
        return cmd_viewfolder(ctx, argc, argv);
    } else {
        fprintf(stderr, "Unknown command: %s (type 'help' for available commands)\n", cmd);
        return 0;
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    client_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    
    strncpy(ctx.nm_host, argv[1], sizeof(ctx.nm_host) - 1);
    ctx.nm_port = (uint16_t)strtoul(argv[2], NULL, 10);
    ctx.client_port = 7000;
    
    printf("Enter your username: ");
    fflush(stdout);
    if (fgets(ctx.username, sizeof(ctx.username), stdin) == NULL) {
        fprintf(stderr, "Failed to read username\n");
        return EXIT_FAILURE;
    }
    
    size_t len = strlen(ctx.username);
    if (len > 0 && ctx.username[len - 1] == '\n') {
        ctx.username[len - 1] = '\0';
    }
    
    if (strlen(ctx.username) == 0) {
        fprintf(stderr, "Username cannot be empty\n");
        return EXIT_FAILURE;
    }
    
    if (register_client(&ctx) != 0) {
        return EXIT_FAILURE;
    }
    
    /* Clear screen */
    printf("\033[H\033[J");
    
printf("%s", COLOR_CYAN);
printf("  ____   ___   ____    ____         \n");
printf(" |  _ \\ / _ \\ / ___|  / ___|      _         _  \n");
printf(" | | | | | | | |      \\___ \\   __| |__   __| |__ \n");
printf(" | |_| | |_| | |___    ___) | |__   __| |__   __|\n");
printf(" |____/ \\___/ \\____|  |____/     |_|       |_|\n");


    printf("%s", COLOR_RESET);
    printf("%s   Distributed File System Client%s\n", COLOR_BOLD, COLOR_RESET);
    printf("   %sCS3-OSN Monsoon 2025%s\n\n", COLOR_YELLOW, COLOR_RESET);
    
    printf("Type '%shelp%s' for commands, '%sexit%s' to quit\n\n", COLOR_BOLD, COLOR_RESET, COLOR_BOLD, COLOR_RESET);
    
    char line[2048];
    while (1) {
        printf("%s%s%s@docs++%s > ", COLOR_BOLD, COLOR_CYAN, ctx.username, COLOR_RESET);
        fflush(stdout);
        
        if (fgets(line, sizeof(line), stdin) == NULL) {
            /* Check if it's EOF or an error */
            if (feof(stdin)) {
                /* EOF - user pressed Ctrl+D or stdin closed */
                break;
            }
            if (ferror(stdin)) {
                /* Error reading stdin */
                fprintf(stderr, "\nError reading input\n");
                clearerr(stdin);
                continue;
            }
            /* Shouldn't reach here, but break just in case */
            break;
        }
        
        if (parse_and_execute(&ctx, line) < 0) {
            break;
        }
    }
    
    printf("\nGoodbye!\n");
    return EXIT_SUCCESS;
}

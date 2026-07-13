#define _POSIX_C_SOURCE 200809L

#include "storage_engine.h"
#include "persistence.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void remove_recursive(const char *path) {
    char command[512];
    snprintf(command, sizeof(command), "rm -rf %s", path);
    system(command);
}

int main(void) {
    char tempdir[] = "/tmp/ss_engineXXXXXX";
    char *root = mkdtemp(tempdir);
    if (root == NULL) {
        perror("mkdtemp");
        return EXIT_FAILURE;
    }

    storage_engine_t *engine = NULL;
    assert(storage_engine_init(&engine, root, "unit") == 0);

    assert(storage_engine_create_file(engine, "doc.txt", "alice") == STORAGE_OK);

    char *content = NULL;
    assert(storage_engine_read(engine, "doc.txt", &content) == STORAGE_OK);
    assert(strcmp(content, "") == 0);
    free(content);

    storage_word_update_t updates1[] = {
        {.index = 0, .text = "Hello world."}
    };
    assert(storage_engine_write(engine, "doc.txt", 0, updates1, 1, NULL, 0) == STORAGE_OK);

    assert(storage_engine_read(engine, "doc.txt", &content) == STORAGE_OK);
    assert(strcmp(content, "Hello world.") == 0);
    free(content);

    storage_file_info_t info;
    assert(storage_engine_info(engine, "doc.txt", &info) == STORAGE_OK);
    assert(info.word_count == 2);
    assert(info.sentence_count == 1);
    assert(strcmp(info.owner, "alice") == 0);

    storage_word_update_t updates2[] = {
        {.index = 1, .text = "More adventures await!"}
    };
    char undo_token[64];
    assert(storage_engine_write(engine, "doc.txt", 0, updates2, 1, undo_token, sizeof(undo_token)) == STORAGE_OK);
    assert(strlen(undo_token) > 0);

    assert(storage_engine_read(engine, "doc.txt", &content) == STORAGE_OK);
    assert(strcmp(content, "Hello More adventures await! world.") == 0);
    free(content);

    assert(storage_engine_info(engine, "doc.txt", &info) == STORAGE_OK);
    assert(info.sentence_count == 2);

    char meta_path[512];
    char file_path[512];
    char undo_path[512];
    assert(ss_metadata_path(meta_path, sizeof(meta_path), root, "unit", "doc.txt") == 0);
    FILE *fp = fopen(meta_path, "r");
    assert(fp != NULL);
    char buffer[512];
    size_t read = fread(buffer, 1, sizeof(buffer) - 1, fp);
    buffer[read] = '\0';
    fclose(fp);
    assert(strstr(buffer, "\"owner\":\"alice\"") != NULL);
    assert(strstr(buffer, "\"sentence_count\":2") != NULL);

    assert(storage_engine_undo(engine, "doc.txt") == STORAGE_OK);
    assert(storage_engine_read(engine, "doc.txt", &content) == STORAGE_OK);
    assert(strcmp(content, "Hello world.") == 0);
    free(content);

    assert(ss_file_path(file_path, sizeof(file_path), root, "unit", "doc.txt") == 0);
    assert(ss_undo_path(undo_path, sizeof(undo_path), root, "unit", "doc.txt") == 0);
    fp = fopen(meta_path, "r");
    assert(fp != NULL);
    read = fread(buffer, 1, sizeof(buffer) - 1, fp);
    buffer[read] = '\0';
    fclose(fp);
    assert(strstr(buffer, "\"sentence_count\":1") != NULL);

    storage_engine_destroy(engine);
    remove_recursive(root);
    return EXIT_SUCCESS;
}

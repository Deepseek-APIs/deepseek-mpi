#ifndef FILE_LOADER_H
#define FILE_LOADER_H

#include <stddef.h>
#include <stdio.h>

int file_loader_read_all(const char *path, char **out, size_t *len, char **error_out);
int file_loader_read_stream(FILE *stream, char **out, size_t *len, char **error_out);

#endif /* FILE_LOADER_H */

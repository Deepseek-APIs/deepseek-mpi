#ifndef INPUT_CHUNKER_H
#define INPUT_CHUNKER_H

#include <stddef.h>

typedef struct {
  size_t chunk_size;
  size_t total_length;
  int rank;
  int world_size;
  size_t cursor;
} ChunkCursor;

void chunk_cursor_init(ChunkCursor *cursor, size_t chunk_size, size_t total_length, int rank, int world_size);
int chunk_cursor_next(ChunkCursor *cursor, size_t *start, size_t *end, size_t *chunk_index);

#endif /* INPUT_CHUNKER_H */

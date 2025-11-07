#include "input_chunker.h"

void chunk_cursor_init(ChunkCursor *cursor, size_t chunk_size, size_t total_length, int rank, int world_size) {
  if (!cursor) {
    return;
  }
  cursor->chunk_size = chunk_size;
  cursor->total_length = total_length;
  cursor->rank = rank;
  cursor->world_size = world_size <= 0 ? 1 : world_size;
  cursor->cursor = 0;
}

int chunk_cursor_next(ChunkCursor *cursor, size_t *start, size_t *end, size_t *chunk_index) {
  if (!cursor || cursor->chunk_size == 0) {
    return 0;
  }
  size_t global_index = (size_t) cursor->rank + cursor->cursor * (size_t) cursor->world_size;
  size_t begin = global_index * cursor->chunk_size;
  if (begin >= cursor->total_length) {
    return 0;
  }
  size_t finish = begin + cursor->chunk_size;
  if (finish > cursor->total_length) {
    finish = cursor->total_length;
  }
  if (start) {
    *start = begin;
  }
  if (end) {
    *end = finish;
  }
  if (chunk_index) {
    *chunk_index = global_index;
  }
  cursor->cursor += 1;
  return 1;
}

#include <string.h>
#include <stdlib.h>
#include "str.h"
#include "utf8.h"

// Initialize an empty string at s.
void str_init(str *s) {
  s->mem = NULL;
  s->chars[0] = '\0';
}

// Initialize a string with the specified content.
void str_init2(str *s, const uint8_t *content) {
  size_t num_bytes = strlen((char *)content);
  size_t num_chars = strlen_utf8(content);
  str_init3(s, content, num_bytes, num_chars);
}

// Initialize a string with the specified content, occupying the specified
// number of bytes & characters.
void str_init3(str *s, const uint8_t *content, size_t num_bytes, size_t num_chars) {
  if (num_bytes < sizeof(s->chars)) {
    // Inlined.
    s->mem = NULL;
    memcpy(s->chars, content, num_bytes);
    s->chars[num_bytes] = '\0';
  } else {
    s->mem = (uint8_t *)malloc(num_bytes + 1); // We'll put a \0 on it.
    memcpy(s->mem, content, num_bytes);
    s->mem[num_bytes] = '\0';
    s->num_bytes = num_bytes;
    s->num_chars = num_chars;
  }
}

void str_init_with_substring(str *s, str *other, size_t start, size_t length) {
  // Count out bytes.
  size_t other_len = str_num_chars(other);
  if (start > other_len) {
    return str_init(s);
  } else if (start + length > other_len) {
    length = other_len - start;
  }
  
  uint8_t *range_start = count_utf8_chars(str_content(other), start);
  uint8_t *range_end = count_utf8_chars(range_start, length);
  
  str_init3(s, range_start, range_end - range_start, length);
}

void str_destroy(str *s) {
  if (s->mem) {
    free(s->mem);
  }
}

static void _append(str *s, const uint8_t *other, size_t other_bytes, size_t other_chars) {
  if (s->mem) {
    s->mem = realloc(s->mem, s->num_bytes + other_bytes + 1);
    memcpy(&s->mem[s->num_bytes], other, other_bytes);
    s->num_bytes += other_bytes;
    s->num_chars += other_chars;
    s->mem[s->num_bytes] = '\0';
  } else {
    size_t my_bytes = str_num_bytes(s);
    if (my_bytes + other_bytes >= sizeof(s->chars)) {
      // Expand.
      size_t my_chars = strlen_utf8(s->chars);
      uint8_t *mem = (uint8_t *)malloc(my_bytes + other_bytes + 1);
      memcpy(mem, s->chars, my_bytes);
      memcpy(&mem[my_bytes], other, other_bytes);
      s->mem = mem;
      s->num_bytes = my_bytes + other_bytes;
      s->num_chars = my_chars + other_chars;
      mem[s->num_bytes] = '\0';
    } else {
      // Append inline.
      memcpy(&s->chars[my_bytes], other, other_bytes);
      s->chars[my_bytes + other_bytes] = '\0';
    }
  }
}

void str_append(str *s, const str *other) {
  _append(s, str_content(other), str_num_bytes(s), str_num_chars(s));
}

void str_append2(str *s, const uint8_t *other) {
  _append(s, other, strlen((char *)other), strlen_utf8(other));
}
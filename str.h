// This is a tiny utf-8 string library. If strings are less than 12 characters (or 24 chars
// in 64 bit mode), they are inlined in the struct.
//
// This data format is ideal for small strings (<100 bytes in size). If the strings are
// larger, consider using ropes.

#ifndef smallstring_h
#define smallstring_h

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "utf8.h"

// It might make sense to increase this sometimes for performance.
#define STR_MAX_INLINE (sizeof(size_t) * 2)

typedef struct {
  uint8_t *mem; // NULL if the string is inline.
  union {
    // If the string takes up more than a few bytes
    struct {
      size_t num_bytes;
      size_t num_chars;
    };
    // Inline characters
    uint8_t chars[STR_MAX_INLINE];
  };
} str;

// Initialize an empty string at s.
void str_init(str *s);

// Initialize a string with the specified content.
void str_init2(str *s, const uint8_t *content);

// Initialize a string with the specified content, occupying the specified
// number of bytes & characters.
void str_init3(str *s, const uint8_t *content, size_t num_bytes, size_t num_chars);

// Initialize a string with the contents of another string
void str_init_with_copy(str *dest, const str *src);

// Initialize a string with a substring of another string.
void str_init_with_substring(str *s, str *other, size_t start, size_t length);

void str_destroy(str *s);

// Get the number of characters in a string
static inline size_t str_num_chars(const str *s) {
  return s->mem ? s->num_chars : strlen_utf8(s->chars);
}

// Get the number of bytes in a string
static inline size_t str_num_bytes(const str *s) {
  return s->mem ? s->num_bytes : strlen((const char *)s->chars);
}

// Append other to s.
void str_append(str *s, const str *other);
void str_append2(str *s, const uint8_t *other);

static inline const uint8_t *str_content(const str *s) {
  return s->mem ? s->mem : s->chars;
}


#endif

/*
 * Ops are lists of components which iterate over the document.
 *
 * Components are either:
 *
 * A number N: Skip N characters in the original document
 * {INSERT, 'str'}: Insert 'str' at the current position in the document
 * {DELETE, N}:     Delete N characters at the current pos
 *
 * Eg: [3, {INSERT, 'hi'}, 5, {DELETE, 9}]
 */

#ifndef OT_text_composable_h
#define OT_text_composable_h

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "str.h"
#include "rope.h"

typedef enum {
  TEXT_OP_NONE = 0,
  TEXT_OP_SKIP = 1,
  TEXT_OP_INSERT = 3,
  TEXT_OP_DELETE = 4,
} text_op_component_type;

typedef struct {
  text_op_component_type type;
  union {
    // If type is SKIP or DELETE
    size_t num;
    // If type is INSERT
    str str;
  };
} text_op_component;

typedef struct {
  text_op_component *components; // NULL or a pointer.
  union {
    // If components is NULL, the op component is stored inline
    struct {
      // Most ops are just one edit at a location.
      size_t skip;
      text_op_component content;
    };
    // If components is not null, it contains a vector of components
    struct {
      // For more complicated ops, components points to the data and we use these fields.
      size_t num_components;
      size_t capacity;
    };
  };
} text_op;

// This represents an area of text in the document that a user is focussed on. Most of the time the
// user hasn't really selected anything, and the start and end points just point to wherever the
// user's cursor is.
// There is no requirement for the start to be before the end - in HTML parlance, the start
// refers to the selection anchor (which is where the selection started), and the end represents
// the selection's focus.
typedef struct {
  size_t start;
  size_t end;
} text_cursor;

void text_op_from_components2(text_op *dest, text_op_component components[], size_t num);

// Returns bytes read on success, negative on failure.
ssize_t text_op_from_bytes(text_op *dest, void *bytes, size_t num_bytes);

typedef void (*text_write_fn)(void *bytes, size_t num, void *user);
void text_op_to_bytes(text_op *op, text_write_fn write, void *user);

void text_op_clone2(text_op *dest, text_op *src);
void text_op_transform2(text_op *result, text_op *op, text_op *other, bool isLefthand);
void text_op_compose2(text_op *result, text_op *op1, text_op *op2);


// Create and return a new text op which inserts the specified string at pos.
text_op text_op_insert(size_t pos, const uint8_t *str);

// Create a new text op which deletes the specified number of characters
text_op text_op_delete(size_t pos, size_t amt);

void text_op_free(text_op *op);

// Initialize an op from an array of op components. Existing content in dest is ignored.
static inline text_op text_op_from_components(text_op_component components[], size_t num) {
  text_op result;
  text_op_from_components2(&result, components, num);
  return result;
}

// Make a copy of an op.
static inline text_op text_op_clone(text_op *src) {
  text_op result;
  text_op_clone2(&result, src);
  return result;
}

// Write the op out to standard out.
void text_op_print(const text_op *op);

// Apply an operation to the specified document.
// returns 0 on success, nonzero on failure.
int text_op_apply(rope *doc, text_op *op);

// Check if an op is valid and could be applied to the given document. Returns zero on success,
// nonzero on failure.
int text_op_check(const rope *doc, const text_op *op);

// Transform an op by another op.
// isLeftHand is used to break ties when both ops insert at the same position in the document.
static inline text_op text_op_transform(text_op *op, text_op *other, bool isLefthand) {
  text_op result;
  text_op_transform2(&result, op, other, isLefthand);
  return result;
}

// Compose 2 ops together to produce a single operation. When the result is applied to a document,
// it has the same effect as applying op1 followed by op2.
static inline text_op text_op_compose(text_op *op1, text_op *op2) {
  text_op result;
  text_op_compose2(&result, op1, op2);
  return result;
}

static inline text_cursor text_cursor_make(size_t start, size_t end) {
  return (text_cursor){start, end};
}

// Check if a cursor is valid. Returns zero on success, nonzero on failure (the cursor is invalid).
int text_cursor_check(const rope *doc, text_cursor cursor);

// Transform a cursor by an operation. is_own_op is set if the operation was sent by the cursor's
// owner.
text_cursor text_op_transform_cursor(text_cursor cursor, const text_op *op, bool is_own_op);

#endif

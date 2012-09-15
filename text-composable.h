//
//  text-composable.h
//  OT
//
//  Created by Joseph Gentle on 3/09/12.
//  Copyright (c) 2012 Joseph Gentle. All rights reserved.
//

#ifndef OT_text_composable_h
#define OT_text_composable_h

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "str.h"
#include "rope.h"

typedef enum {
	NONE = 0,
	SKIP = 1,
	INSERT = 3,
	DELETE = 4,
} component_type;

#define INLINE_STRING_LEN 8

typedef struct {
	component_type type;
	union {
    // For skips and deletes
		size_t num;
    
    str str;
	};
} text_op_component;

typedef struct {
  text_op_component *components; // NULL or a pointer.
  union {
    struct {
      // Most ops are just one edit at a location.
      size_t skip;
      text_op_component content;
    };
    struct {
      // For more complicated ops, components points to the data and we use these fields.
      size_t num_components;
      size_t capacity;
    };
  };
} text_op;

typedef rope text_doc;

void text_op_free(text_op *op);

// Initialize an op from an array of op components. Existing content in dest is ignored.
void text_op_from_components(text_op *dest, text_op_component components[], size_t num);

// Create and return a new text op which inserts the specified string at pos.
text_op text_op_insert(size_t pos, const uint8_t *str);

// Create a new text op which deletes the specified number of characters
text_op text_op_delete(size_t pos, size_t amt);

void text_op_clone(text_op *dest, text_op *src);

void text_op_print(text_op *op);

// returns 0 on success, nonzero on failure.
int text_op_apply(text_doc *doc, text_op *op);

// Check if an op is valid and could be applied to the given document. Returns zero on success,
// nonzero on failure.
int text_op_check(text_doc *doc, text_op *op);

void text_op_transform2(text_op *result, text_op *op, text_op *other, bool isLefthand);

inline static text_op text_op_transform(text_op *op, text_op *other, bool isLefthand) {
  text_op result;
  text_op_transform2(&result, op, other, isLefthand);
  return result;
}

void text_op_compose2(text_op *result, text_op *op1, text_op *op2);
inline static text_op text_op_compose(text_op *op1, text_op *op2) {
  text_op result;
  text_op_compose2(&result, op1, op2);
  return result;
}

#endif

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
  size_t num_components;
  size_t capacity;
  text_op_component components[];
  /*
  union {
    struct {
      size_t num_components;
      size_t capacity;
    };
    struct {
      size_t offset;
      text_op_component content;
    };
  };*/
} text_op;

typedef rope text_doc;

text_op *text_op_create();
void text_op_free(text_op *op);
text_op *text_op_from_components(text_op_component components[], size_t num);

void text_op_print(text_op *op);

// returns 0 on success, nonzero on failure.
int text_op_apply(text_doc *doc, text_op *op);

// Check if an op is valid and could be applied to the given document.
int text_op_check(text_doc *doc, text_op *op);

text_op *text_op_transform(text_op *op, text_op *other, bool isLefthand);

text_op *text_op_compose(text_op *op1, text_op *op2);

#endif

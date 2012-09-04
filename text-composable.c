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
 *
 * Snapshots are strings.
 */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "text-composable.h"

static text_op *ensure_capacity(text_op *op) {
  if (op->num_components == op->capacity) {
    op->capacity *= 2;
    op = realloc(op, sizeof(text_op) * op->capacity * sizeof(text_op_component));
  }
  return op;
}

static text_op *append(text_op *op, text_op_component c) {
  if (c.type == NONE
      || ((c.type == SKIP || c.type == DELETE) && c.num == 0)
      || (c.type == INSERT && !c.str.mem && c.str.chars[0] == '\0')) {
    // We're not inserting any actual data. Skip.
    return op;
  } else if (op->num_components == 0) {
    // The list is empty. Create a new node.
    op = ensure_capacity(op);
    op->components[0] = c;
    op->num_components++;
  } else {
    text_op_component *lastC = &op->components[op->num_components - 1];
    if (lastC->type == c.type) {
      if (lastC->type == DELETE || lastC->type == SKIP) {
        // Extend the delete / skip component.
        lastC->num += c.num;
      } else {
        // Extend the insert component.
        str_append(&lastC->str, &c.str);
      }
    } else {
      op = ensure_capacity(op);
      op->components[op->num_components++] = c;
    }
  }
  
  return op;
}

text_op *text_op_create() {
  text_op *op = (text_op *)malloc(sizeof(text_op) + sizeof(text_op_component) * 3);
  op->capacity = 3;
  op->num_components = 0;
  return op;
}

void text_op_free(text_op *op) {
  for (int i = 0; i < op->num_components; i++) {
    if (op->components[i].type == INSERT) {
      str_destroy(&op->components[i].str);
    }
  }
  free(op);
}

text_op *text_op_from_components(text_op_component components[], size_t num) {
  text_op *op = (text_op *)malloc(sizeof(text_op) + sizeof(text_op_component) * num);
  op->capacity = num;
  op->num_components = 0;
  
  for (int i = 0; i < num; i++) {
    append(op, components[i]);
  }
  return op;
}


static void print_op_component(text_op_component component) {
	switch (component.type) {
		case SKIP:
			printf("Skip   : %lu", component.num);
			break;
		case INSERT:
			printf("Insert : %lu ('%s')", str_num_chars(&component.str), str_content(&component.str));
			break;
		case DELETE:
			printf("Delete : %lu", component.num);
			break;
		default:
			break;
	}
	printf("\n");
	fflush(stdout);
}

void text_op_print(text_op *op) {
	for (int i = 0; i < op->num_components; i++) {
		printf("%d.\t", i);
		print_op_component(op->components[i]);
	}
	printf("\n");
}

// Faster or slower taking a pointer?
static size_t component_length(const text_op_component *c) {
  switch (c->type) {
    case NONE:
      return 0;
    case SKIP:
    case DELETE:
      return c->num;
    case INSERT:
      return str_num_chars(&c->str);
  }
}

typedef struct {
  size_t idx;
  size_t offset;
} op_iter;

#define MIN(x,y) ((x) > (y) ? (y) : (x))

static text_op_component take(text_op *op, op_iter *iter, size_t max_len, component_type indivisible_type) {
  if (iter->idx == op->num_components) {
    return (text_op_component){};
  }

  // Faster or slower with a pointer?
  text_op_component e = op->components[iter->idx];
  size_t length = component_length(&e);

  if (e.type == indivisible_type) {
    max_len = length - iter->offset;
  } else {
    max_len = MIN(max_len, length - iter->offset);
  }

  if (e.type == INSERT) {
    if (max_len < length) {
      str_init_with_substring(&e.str, &op->components[iter->idx].str, iter->offset, max_len);
      if (max_len + iter->offset == length) {
        // This is the last part of the string we're taking. Free it.
        str_destroy(&op->components[iter->idx].str);
      }
    }
  } else {
    e.num = max_len;
  }

  // Update the iterator.
  iter->offset += max_len;
  assert(iter->offset <= length);

  if (iter->offset >= length) {
    iter->offset = 0;
    iter->idx++;
  }

  return e;
}

text_op *text_op_transform(text_op *op, text_op *other, bool isLefthand) {
  op_iter iter = {};
  
  text_op *result = text_op_create();
  
  for (int i = 0; i < other->num_components; i++) {
    switch (other->components[i].type) {
      case SKIP: {
        size_t num = other->components[i].num;
        
        while (num > 0) {
          text_op_component c = take(op, &iter, num, INSERT);
          result = append(result, c);
          if (c.type != INSERT) {
            num -= c.num;
          }
        }
        break;
      }
      case INSERT: {
        if (isLefthand
            && iter.idx < op->num_components
            && op->components[iter.idx].type == INSERT) {
          // The left insert goes first.
          result = append(result, take(op, &iter, SIZE_MAX, NONE));
        }
        text_op_component skip = {SKIP};
        skip.num = other->components[i].num;
        result = append(result, skip);
        break;
      }
      case DELETE: {
        size_t num = other->components[i].num;
        
        while (num > 0) {
          text_op_component c = take(op, &iter, num, INSERT);
          
          switch (c.type) {
            case SKIP:
              num -= c.num;
              break;
            case INSERT:
              result = append(result, c);
              break;
            case DELETE:
              // The delete is unnecessary now.
              num -= c.num;
              break;
            default:
              assert(0);
          }
        }
        break;
      }
      default:
        assert(0);
    }
  }
  
  while (iter.idx < op->num_components) {
    // The op doesn't have skips at the end. Just copy everything.
    result = append(result, take(op, &iter, SIZE_MAX, NONE));
  }
  
  free(op);
  return result;
}

text_op *text_op_compose(text_op *op1, text_op *op2) {
  text_op *result = text_op_create();
  op_iter iter = {};
  
  for (int i = 0; i < op2->num_components; i++) {
    switch (op2->components[i].type) {
      case SKIP: {
        size_t num = op2->components[i].num;
        
        while (num > 0) {
          text_op_component c = take(op1, &iter, num, DELETE);
          append(result, c);
          if (c.type != DELETE) {
            num -= component_length(&c);
          }
        }
        break;
      }
      case INSERT:
        append(result, op2->components[i]);
        break;
      case DELETE: {
        size_t offset = 0;
        size_t clen = op2->components[i].num;
        while (offset < clen) {
          text_op_component c = take(op1, &iter, clen - offset, DELETE);
          // If its skip, drop it and decrease length.
          // If its insert, check the strings match, drop it and decrease length.
          // If its delete, append it.
          switch (c.type) {
            case SKIP: {
              c.type = DELETE;
              append(result, c);
              offset += c.num;
              break;
            }
            case INSERT:
              // op1 has inserted text, then op2 deleted it again.
              offset += str_num_chars(&c.str);
              break;
            case DELETE:
              append(result, c);
              break;
            default:
              assert(0);
          }
        }
        break;
      default:
        assert(0);
      }
    }
  }
  
  while (iter.idx < op1->num_components) {
    // The op doesn't have skips at the end. Just copy everything.
    append(result, take(op1, &iter, SIZE_MAX, NONE));
  }
  
  free(op1);
  free(op2);
  
  return result;
}


int text_op_check(text_doc *doc, text_op *op) {
  size_t doc_length = rope_char_count(doc);
  size_t pos = 0;
  
  for (int i = 0; i < op->num_components; i++) {
    if (i >= 1) {
      // Check the component type is different from the preceeding component.
      if (op->components[i].type == op->components[i - 1].type) {
        return 1;
      }
    }
    switch (op->components[i].type) {
      case SKIP: {
        size_t num = op->components[i].num;
        if (num == 0) {
          return 1;
        }
        
        pos += num;
        
        if (pos > doc_length) {
          return 1;
        }
        break;
      }
      case INSERT: {
        size_t slen = str_num_chars(&op->components[i].str);
        if (slen == 0) {
          return 1;
        }
        doc_length += slen;
        pos += slen;
        break;
      }
      case DELETE: {
        size_t num = op->components[i].num;
        if (num == 0 || doc_length < pos + num) {
          return 1;
        }
        
        doc_length -= num;
        break;
      }
      default:
        return 1;
    }
  }
  return 0;
}

int text_op_apply(text_doc *doc, text_op *op) {
#ifdef DEBUG
  if (text_op_check(doc, op)) {
    return 1;
  }
#endif
  
  size_t pos = 0;
  for (int i = 0; i < op->num_components; i++) {
    switch (op->components[i].type) {
      case SKIP:
        pos += op->components[i].num;
        break;
      case INSERT:
        rope_insert(doc, pos, str_content(&op->components[i].str));
        break;
      case DELETE:
        rope_del(doc, pos, op->components[i].num);
        break;
      default:
        return 1;
    }
  }
  
  return 0;
}



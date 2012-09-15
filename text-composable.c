/* Implementation of the text-composable OT type.
 *
 * See header file.
 */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "text-composable.h"

// Check that the given op has enough space for an additional op component.
static void ensure_capacity(text_op *op) {
  if (op->components == NULL && op->content.type != NONE) {
    // Grow the op into a big op.
    text_op_component *components = op->components = malloc(sizeof(text_op_component) * 4);
    if (op->skip) {
      components[0].type = SKIP;
      components[0].num = op->skip;
      components[1] = op->content;
      op->num_components = 2;
    } else {
      components[0] = op->content;
      op->num_components = 1;
    }
    op->capacity = 4;
  } else if (op->components != NULL && op->num_components == op->capacity) {
    op->capacity *= 2;
    op->components = realloc(op->components, op->capacity * sizeof(text_op_component));
  }
}

// Clone a component. This is only hard because long inserts need allocing.
static text_op_component copy_component(const text_op_component old) {
  if (old.type != INSERT) {
    return old;
  } else {
    text_op_component c = {INSERT};
    str_init_with_copy(&c.str, &old.str);
    return c;
  }
}

// Append the specified component to the end of the op.
static void append(text_op *op, const text_op_component c) {
  if (c.type == NONE
      || ((c.type == SKIP || c.type == DELETE) && c.num == 0)
      || (c.type == INSERT && !c.str.mem && c.str.chars[0] == '\0')) {
    // We're not inserting any actual data. Skip.
    return;
  } else if (op->components == NULL) {
    // Small op.
    if (op->content.type == NONE) {
      if (c.type == SKIP) {
        op->skip += c.num;
      } else {
        op->content = copy_component(c);
      }
      return;
    } else if (op->content.type == c.type) {
      if (c.type == DELETE) {
        op->content.num += c.num;
        return;
      } else if (c.type == INSERT) {
        str_append(&op->content.str, &c.str);
        return;
      }
    }
    
    // Fall through here if the small op can't hold the new component. Expand it and append.
    ensure_capacity(op);
    op->components[op->num_components++] = copy_component(c);
  } else {
    // Big op.
    if (op->num_components == 0) { // This will basically never happen.
      // The list is empty. Create a new node.
      ensure_capacity(op);
      op->components[0] = copy_component(c);
      op->num_components++;
    } else {
      text_op_component *lastC = &op->components[op->num_components - 1];
      if (lastC->type == c.type) {
        if (c.type == DELETE || c.type == SKIP) {
          // Extend the delete / skip component.
          lastC->num += c.num;
        } else {
          // Extend the insert component.
          str_append(&lastC->str, &c.str);
        }
      } else {
        ensure_capacity(op);
        op->components[op->num_components++] = copy_component(c);
      }
    }
  }
}

void text_op_clone2(text_op *dest, text_op *src) {
  if (src->components) {
    size_t num = src->num_components;
    dest->components = malloc(sizeof(text_op_component) * num);
    dest->capacity = dest->num_components = num;
    for (int i = 0; i < num; i++) {
      dest->components[i] = copy_component(src->components[i]);
    }
  } else {
    dest->components = NULL;
    dest->skip = src->skip;
    dest->content = copy_component(src->content);
  }
}

text_op text_op_insert(size_t pos, const uint8_t *str) {
  text_op op;
  op.components = NULL;
  op.skip = pos;
  op.content.type = INSERT;
  str_init2(&op.content.str, str);
  return op;
}

text_op text_op_delete(size_t pos, size_t amt) {
  text_op op;
  op.components = NULL;
  op.skip = pos;
  op.content.type = DELETE;
  op.content.num = amt;
  return op;
}

void text_op_free(text_op *op) {
  if (op->components) {
    for (int i = 0; i < op->num_components; i++) {
      if (op->components[i].type == INSERT) {
        str_destroy(&op->components[i].str);
      }
    }
    free(op->components);
  } else if (op->content.type == INSERT) {
    str_destroy(&op->content.str);
  }
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

void text_op_from_components2(text_op *dest, text_op_component components[], size_t num) {
  // Consider rewriting this to take advantage of knowing num - we could probably pick a pretty
  // good initial capacity for the op.
  dest->components = NULL;
  dest->skip = 0;
  dest->content.type = NONE;
  
  // Pre-emptively discard skips in the component list.
  while (num && (components[num - 1].type == SKIP || component_length(&components[num - 1]) == 0)) {
    num--;
  }
  for (int i = 0; i < num; i++) {
    append(dest, components[i]);
    if (components[i].type == INSERT) {
      str_destroy(&components[i].str);
    }
  }
}

static void component_print(text_op_component component) {
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
  if (op->components) {
    for (int i = 0; i < op->num_components; i++) {
      printf("%d.\t", i);
      component_print(op->components[i]);
    }
    printf("\n");
  } else {
    printf("At %lu ", op->skip);
    component_print(op->content);
  }
}

typedef struct {
  size_t idx;
  size_t offset;
} op_iter;

#define MIN(x,y) ((x) > (y) ? (y) : (x))

static text_op_component take(text_op *op, op_iter *iter, size_t max_len, component_type indivisible_type) {
  // Faster or slower with a pointer?
  text_op_component e;
  
  if (op->components == NULL) {
    // idx will be 0 or 1 for the two components.
    if (iter->idx == 0) {
      if (op->skip) {
        e.type = SKIP;
        e.num = op->skip;
      } else {
        iter->idx++;
        iter->offset = 0;
        e = op->content;
      }
    } else if (iter->idx == 1) {
      e = op->content;
    } else {
      return (text_op_component){};
    }
  } else {
    if (iter->idx == op->num_components) {
      return (text_op_component){};
    }

    e = op->components[iter->idx];
  }
  
  size_t length = component_length(&e);

  if (e.type == indivisible_type) {
    max_len = length - iter->offset;
  } else {
    max_len = MIN(max_len, length - iter->offset);
  }

  if (e.type == INSERT) {
    if (max_len < length) {
      str_init_with_substring(&e.str, &op->components[iter->idx].str, iter->offset, max_len);
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

inline static component_type peek_type(text_op *op, op_iter iter) {
  if (op->components) {
    return iter.idx < op->num_components ? op->components[iter.idx].type : NONE;
  } else {
    return iter.idx < 2 ? (iter.idx == 0 && op->skip ? SKIP : op->content.type) : NONE;
  }
}

inline static void init_op(text_op *op) {
  op->components = NULL;
  op->skip = 0;
  op->content.type = NONE;
}

void text_op_transform2(text_op *result, text_op *op, text_op *other, bool isLefthand) {
  init_op(result);
  
  if (op->components == NULL && op->content.type == NONE) {
    return;
  }
  
  op_iter iter = {};
  
  text_op_component *other_components = other->components;
  size_t num_other_components;
  
  text_op_component inline_components[2];
  if (other_components == NULL) {
    other_components = inline_components;
    if (other->content.type == NONE) {
      num_other_components = 0;
    } else if (other->skip == 0) {
      num_other_components = 1;
      inline_components[0] = other->content;
    } else {
      num_other_components = 2;
      inline_components[0].type = SKIP;
      inline_components[0].num = other->skip;
      inline_components[1] = other->content;
    }
  } else {
    num_other_components = other->num_components;
  }
  
  for (int i = 0; i < num_other_components; i++) {
    if (peek_type(op, iter) == NONE) {
      break;
    }
    
    switch (other_components[i].type) {
      case SKIP: {
        size_t num = other_components[i].num;
        
        while (num > 0) {
          text_op_component c = take(op, &iter, num, INSERT);
          if (c.type == NONE) {
            break;
          }
          append(result, c);
          if (c.type != INSERT) {
            num -= c.num;
          }
        }
        break;
      }
      case INSERT: {
        // If isLeftHand and there's an insert next in the current op, the insert should go first.
        if (isLefthand && peek_type(op, iter) == INSERT) {
          // The left insert goes first.
          append(result, take(op, &iter, SIZE_MAX, NONE));
        }
        if (peek_type(op, iter) == NONE) {
          break;
        }
        text_op_component skip = {SKIP};
        skip.num = str_num_chars(&other_components[i].str);
        append(result, skip);
        break;
      }
      case DELETE: {
        size_t num = other_components[i].num;
        
        while (num > 0) {
          text_op_component c = take(op, &iter, num, INSERT);
          
          switch (c.type) {
            case NONE:
              num = 0;
              break;
            case SKIP:
              num -= c.num;
              break;
            case INSERT:
              append(result, c);
              break;
            case DELETE:
              // The delete is unnecessary now.
              num -= c.num;
              break;
          }
        }
        break;
      }
      default:
        assert(0);
    }
  }
  
  while (iter.idx < (op->components ? op->num_components : 2)) {
    // The op doesn't have skips at the end. Just copy everything.
    append(result, take(op, &iter, SIZE_MAX, NONE));
  }
  
  // Trim any trailing skips from the result.
  if (result->components) {
    while (result->num_components && result->components[result->num_components - 1].type == SKIP) {
      result->num_components--;
    }
  }
}

void text_op_compose2(text_op *result, text_op *op1, text_op *op2) {
  init_op(result);
  op_iter iter = {};
  
  text_op_component *op2_c = op2->components;
  size_t num_op2_c;
  
  text_op_component inline_components[2];
  if (op2_c == NULL) {
    op2_c = inline_components;
    num_op2_c = op2->content.type == NONE ? 0 : 2;
    inline_components[0].type = SKIP;
    inline_components[0].num = op2->skip;
    inline_components[1] = op2->content;
  } else {
    num_op2_c = op2->num_components;
  }
  
  for (int i = 0; i < num_op2_c; i++) {    
    switch (op2_c[i].type) {
      case SKIP: {
        size_t num = op2_c[i].num;
        
        while (num > 0) {
          text_op_component c = take(op1, &iter, num, DELETE);
          if (c.type == NONE) {
            c.type = SKIP;
            c.num = num;
          }
          append(result, c);
          if (c.type != DELETE) {
            num -= component_length(&c);
          }
        }
        break;
      }
      case INSERT:
        append(result, op2_c[i]);
        break;
      case DELETE: {
        size_t offset = 0;
        size_t clen = op2_c[i].num;
        while (offset < clen) {
          text_op_component c = take(op1, &iter, clen - offset, DELETE);
          // If its skip, drop it and decrease length.
          // If its insert, check the strings match, drop it and decrease length.
          // If its delete, append it.
          switch (c.type) {
            case NONE:
              c.num = clen - offset;
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
          }
        }
        break;
      default:
        assert(0);
      }
    }
  }
  
  while (iter.idx < (op1->components ? op1->num_components : 2)) {
    // The op doesn't have skips at the end. Just copy everything.
    append(result, take(op1, &iter, SIZE_MAX, NONE));
  }
}


int text_op_check(rope *doc, text_op *op) {
  size_t doc_length = rope_char_count(doc);
  size_t pos = 0;
  
  if (op->components == NULL) {
    if (op->content.type == NONE) {
      return 0;
    }
    if (op->content.type == SKIP) {
      // If there's content at all, it must be delete or insert.
      return 1;
    }
    
    size_t len = op->content.type == DELETE ? op->content.num : 0;
    if (op->skip + len > doc_length) {
      // Can't delete / skip past the end of the document.
      return 1;
    }
  } else {
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
    if (op->num_components && op->components[op->num_components - 1].type == SKIP) {
      return 1;
    }
  }
  return 0;
}

int text_op_apply(rope *doc, text_op *op) {
#ifdef DEBUG
  if (text_op_check(doc, op)) {
    return 1;
  }
#endif
  
  if (op->components) {
    size_t pos = 0;
    for (int i = 0; i < op->num_components; i++) {
      switch (op->components[i].type) {
        case SKIP:
          pos += op->components[i].num;
          break;
        case INSERT:
          rope_insert(doc, pos, str_content(&op->components[i].str));
          pos += str_num_chars(&op->components[i].str);
          break;
        case DELETE:
          rope_del(doc, pos, op->components[i].num);
          break;
        default:
          return 1;
      }
    }
  } else {
    if (op->content.type == INSERT) {
      rope_insert(doc, op->skip, str_content(&op->content.str));
    } else if (op->content.type == DELETE) {
      rope_del(doc, op->skip, op->content.num);
    }
  }
  return 0;
}



/* Implementation of the text-composable OT type.
 *
 * See header file.
 */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "text.h"

// Check that the given op has enough space for an additional op component.
static void ensure_capacity(text_op *op) {
  if (op->components == NULL && op->content.type != TEXT_OP_NONE) {
    // Grow the op into a big op.
    text_op_component *components = op->components = malloc(sizeof(text_op_component) * 4);
    if (op->skip) {
      components[0].type = TEXT_OP_SKIP;
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
  if (old.type != TEXT_OP_INSERT) {
    return old;
  } else {
    text_op_component c = {TEXT_OP_INSERT};
    str_init_with_copy(&c.str, &old.str);
    return c;
  }
}

// Append the specified component to the end of the op.
static void append(text_op *op, const text_op_component c) {
  if (c.type == TEXT_OP_NONE
      || ((c.type == TEXT_OP_SKIP || c.type == TEXT_OP_DELETE) && c.num == 0)
      || (c.type == TEXT_OP_INSERT && !c.str.mem && c.str.chars[0] == '\0')) {
    // We're not inserting any actual data. Skip.
    return;
  } else if (op->components == NULL) {
    // Small op.
    if (op->content.type == TEXT_OP_NONE) {
      if (c.type == TEXT_OP_SKIP) {
        op->skip += c.num;
      } else {
        op->content = copy_component(c);
      }
      return;
    } else if (op->content.type == c.type) {
      if (c.type == TEXT_OP_DELETE) {
        op->content.num += c.num;
        return;
      } else if (c.type == TEXT_OP_INSERT) {
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
        if (c.type == TEXT_OP_DELETE || c.type == TEXT_OP_SKIP) {
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
  op.content.type = TEXT_OP_INSERT;
  str_init2(&op.content.str, str);
  return op;
}

text_op text_op_delete(size_t pos, size_t amt) {
  text_op op;
  op.components = NULL;
  op.skip = pos;
  op.content.type = TEXT_OP_DELETE;
  op.content.num = amt;
  return op;
}

void text_op_free(text_op *op) {
  if (op->components) {
    for (int i = 0; i < op->num_components; i++) {
      if (op->components[i].type == TEXT_OP_INSERT) {
        str_destroy(&op->components[i].str);
      }
    }
    free(op->components);
  } else if (op->content.type == TEXT_OP_INSERT) {
    str_destroy(&op->content.str);
  }
}

// Faster or slower taking a pointer?
static size_t component_length(const text_op_component *c) {
  switch (c->type) {
    case TEXT_OP_NONE:
      return 0;
    case TEXT_OP_SKIP:
    case TEXT_OP_DELETE:
      return c->num;
    case TEXT_OP_INSERT:
      return str_num_chars(&c->str);
  }
}

void text_op_from_components2(text_op *dest, text_op_component components[], size_t num) {
  // Consider rewriting this to take advantage of knowing num - we could probably pick a pretty
  // good initial capacity for the op.
  dest->components = NULL;
  dest->skip = 0;
  dest->content.type = TEXT_OP_NONE;
  
  // Pre-emptively discard skips in the component list.
  while (num && (components[num - 1].type == TEXT_OP_SKIP || component_length(&components[num - 1]) == 0)) {
    num--;
  }
  for (int i = 0; i < num; i++) {
    append(dest, components[i]);
    if (components[i].type == TEXT_OP_INSERT) {
      str_destroy(&components[i].str);
    }
  }
}

#define CONSUME_BYTES(into, type) if(bytes_remaining < sizeof(type)) return -1; \
  else {\
    (into) = *(type *)bytes;\
    bytes_remaining -= sizeof(type); \
    bytes += sizeof(type); \
  }

ssize_t text_op_from_bytes(text_op *dest, void *bytes, size_t num_bytes) {
  if (num_bytes < sizeof(uint16_t) || bytes == NULL)
    return -1;
  
  size_t bytes_remaining = num_bytes;
  
  dest->components = NULL;
  dest->skip = 0;
  dest->content.type = TEXT_OP_NONE;
  
  // Bytes are:
  // - num components of:
  //   - 1 byte for type
  //   - uint32_t num or str depending on type
  // - 1 byte = zero
  
  while (true) {
    text_op_component component;
    
    // Read a component
    CONSUME_BYTES(component.type, uint8_t);
    if (component.type == 0) break;
    
    switch (component.type) {
      case TEXT_OP_SKIP:
      case TEXT_OP_DELETE:
        CONSUME_BYTES(component.num, uint32_t);
        break;
      case TEXT_OP_INSERT: {
        size_t len = strnlen(bytes, bytes_remaining);
        if (len == bytes_remaining) {
          // Expected a null character at the end of the string
          return -1;
        } else {
          // This is a faked out string - append() will actually copy the string out into
          // the op.
          component.str.mem = bytes;
          component.str.num_bytes = len;
          component.str.num_chars = strlen_utf8(bytes);
          
          // Ignore the \0 as well.
          bytes += len + 1;
          bytes_remaining -= len + 1;
        }
        break;
      }
      default:
        // Unknown type.
        return -1;
    }
    
    append(dest, component);
  }
  
  return num_bytes - bytes_remaining;
}

#undef CONSUME_BYTES

static void write_component(const text_op_component component, text_write_fn write, void *user) {
  uint8_t type = component.type;
  write((void *)&type, 1, user);
  if (component.type == TEXT_OP_INSERT) {
    // Write the string including the \0.
    write((void *)str_content(&component.str), str_num_bytes(&component.str) + 1, user);
  } else {
    write((void *)&component.num, 4, user);
  }
}

void text_op_to_bytes(text_op *op, text_write_fn write, void *user) {
  if (op->components) {
    for (int i = 0; i < op->num_components; i++) {
      write_component(op->components[i], write, user);
    }
  } else {
    if (op->skip) {
      text_op_component skip = {TEXT_OP_SKIP};
      skip.num = op->skip;
      write_component(skip, write, user);
      write_component(op->content, write, user);
    } else {
      if (op->content.type == TEXT_OP_NONE) {
        // Its an empty op. Just say there's 0 components and be done with it.
      } else {
        write_component(op->content, write, user);
      }
    }
  }
  uint8_t zero = 0;
  write((void *)&zero, sizeof(uint8_t), user);
}

static void component_print(text_op_component component) {
	switch (component.type) {
		case TEXT_OP_SKIP:
			printf("Skip   : %zu", component.num);
			break;
		case TEXT_OP_INSERT:
			printf("Insert : %zu ('%s')", str_num_chars(&component.str), str_content(&component.str));
			break;
		case TEXT_OP_DELETE:
			printf("Delete : %zu", component.num);
			break;
		default:
			break;
	}
	printf("\n");
	fflush(stdout);
}

void text_op_print(const text_op *op) {
  if (op->components) {
    for (int i = 0; i < op->num_components; i++) {
      printf("%d.\t", i);
      component_print(op->components[i]);
    }
    printf("\n");
  } else {
    printf("At %zu ", op->skip);
    component_print(op->content);
  }
}

typedef struct {
  size_t idx;
  size_t offset;
} op_iter;

#define MIN(x,y) ((x) > (y) ? (y) : (x))

static text_op_component take(text_op *op, op_iter *iter, size_t max_len,
      text_op_component_type indivisible_type) {
  // Faster or slower with a pointer?
  text_op_component e;
  
  if (op->components == NULL) {
    // idx will be 0 or 1 for the two components.
    if (iter->idx == 0) {
      if (op->skip) {
        e.type = TEXT_OP_SKIP;
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

  if (e.type == TEXT_OP_INSERT) {
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

inline static text_op_component_type peek_type(text_op *op, op_iter iter) {
  if (op->components) {
    return iter.idx < op->num_components ? op->components[iter.idx].type : TEXT_OP_NONE;
  } else {
    return iter.idx < 2 ? (iter.idx == 0 && op->skip ? TEXT_OP_SKIP : op->content.type)
            : TEXT_OP_NONE;
  }
}

inline static void init_op(text_op *op) {
  op->components = NULL;
  op->skip = 0;
  op->content.type = TEXT_OP_NONE;
}

void text_op_transform2(text_op *result, text_op *op, text_op *other, bool isLefthand) {
  init_op(result);
  
  if (op->components == NULL && op->content.type == TEXT_OP_NONE) {
    return;
  }
  
  op_iter iter = {};
  
  text_op_component *other_components = other->components;
  size_t num_other_components;
  
  text_op_component inline_components[2];
  if (other_components == NULL) {
    other_components = inline_components;
    if (other->content.type == TEXT_OP_NONE) {
      num_other_components = 0;
    } else if (other->skip == 0) {
      num_other_components = 1;
      inline_components[0] = other->content;
    } else {
      num_other_components = 2;
      inline_components[0].type = TEXT_OP_SKIP;
      inline_components[0].num = other->skip;
      inline_components[1] = other->content;
    }
  } else {
    num_other_components = other->num_components;
  }
  
  for (int i = 0; i < num_other_components; i++) {
    if (peek_type(op, iter) == TEXT_OP_NONE) {
      break;
    }
    
    switch (other_components[i].type) {
      case TEXT_OP_SKIP: {
        size_t num = other_components[i].num;
        
        while (num > 0) {
          text_op_component c = take(op, &iter, num, TEXT_OP_INSERT);
          if (c.type == TEXT_OP_NONE) {
            break;
          }
          append(result, c);
          if (c.type != TEXT_OP_INSERT) {
            num -= c.num;
          }
        }
        break;
      }
      case TEXT_OP_INSERT: {
        // If isLeftHand and there's an insert next in the current op, the insert should go first.
        if (isLefthand && peek_type(op, iter) == TEXT_OP_INSERT) {
          // The left insert goes first.
          append(result, take(op, &iter, SIZE_MAX, TEXT_OP_NONE));
        }
        if (peek_type(op, iter) == TEXT_OP_NONE) {
          break;
        }
        text_op_component skip = {TEXT_OP_SKIP};
        skip.num = str_num_chars(&other_components[i].str);
        append(result, skip);
        break;
      }
      case TEXT_OP_DELETE: {
        size_t num = other_components[i].num;
        
        while (num > 0) {
          text_op_component c = take(op, &iter, num, TEXT_OP_INSERT);
          
          switch (c.type) {
            case TEXT_OP_NONE:
              num = 0;
              break;
            case TEXT_OP_SKIP:
              num -= c.num;
              break;
            case TEXT_OP_INSERT:
              append(result, c);
              break;
            case TEXT_OP_DELETE:
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
    append(result, take(op, &iter, SIZE_MAX, TEXT_OP_NONE));
  }
  
  // Trim any trailing skips from the result.
  if (result->components) {
    while (result->num_components && result->components[result->num_components - 1].type
           == TEXT_OP_SKIP) {
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
    num_op2_c = op2->content.type == TEXT_OP_NONE ? 0 : 2;
    inline_components[0].type = TEXT_OP_SKIP;
    inline_components[0].num = op2->skip;
    inline_components[1] = op2->content;
  } else {
    num_op2_c = op2->num_components;
  }
  
  for (int i = 0; i < num_op2_c; i++) {    
    switch (op2_c[i].type) {
      case TEXT_OP_SKIP: {
        size_t num = op2_c[i].num;
        
        while (num > 0) {
          text_op_component c = take(op1, &iter, num, TEXT_OP_DELETE);
          if (c.type == TEXT_OP_NONE) {
            c.type = TEXT_OP_SKIP;
            c.num = num;
          }
          append(result, c);
          if (c.type != TEXT_OP_DELETE) {
            num -= component_length(&c);
          }
        }
        break;
      }
      case TEXT_OP_INSERT:
        append(result, op2_c[i]);
        break;
      case TEXT_OP_DELETE: {
        size_t offset = 0;
        size_t clen = op2_c[i].num;
        while (offset < clen) {
          text_op_component c = take(op1, &iter, clen - offset, TEXT_OP_DELETE);
          // If its skip, drop it and decrease length.
          // If its insert, check the strings match, drop it and decrease length.
          // If its delete, append it.
          switch (c.type) {
            case TEXT_OP_NONE:
              c.num = clen - offset;
            case TEXT_OP_SKIP: {
              c.type = TEXT_OP_DELETE;
              append(result, c);
              offset += c.num;
              break;
            }
            case TEXT_OP_INSERT:
              // op1 has inserted text, then op2 deleted it again.
              offset += str_num_chars(&c.str);
              break;
            case TEXT_OP_DELETE:
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
    append(result, take(op1, &iter, SIZE_MAX, TEXT_OP_NONE));
  }
}


int text_op_check(const rope *doc, const text_op *op) {
  size_t doc_length = rope_char_count(doc);
  size_t pos = 0;
  
  if (op->components == NULL) {
    if (op->content.type == TEXT_OP_NONE) {
      return 0;
    }
    if (op->content.type == TEXT_OP_SKIP) {
      // If there's content at all, it must be delete or insert.
      return 1;
    }
    
    size_t len = op->content.type == TEXT_OP_DELETE ? op->content.num : 0;
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
        case TEXT_OP_SKIP: {
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
        case TEXT_OP_INSERT: {
          size_t slen = str_num_chars(&op->components[i].str);
          if (slen == 0) {
            return 1;
          }
          doc_length += slen;
          pos += slen;
          break;
        }
        case TEXT_OP_DELETE: {
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
    if (op->num_components && op->components[op->num_components - 1].type == TEXT_OP_SKIP) {
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
        case TEXT_OP_SKIP:
          pos += op->components[i].num;
          break;
        case TEXT_OP_INSERT:
          rope_insert(doc, pos, str_content(&op->components[i].str));
          pos += str_num_chars(&op->components[i].str);
          break;
        case TEXT_OP_DELETE:
          rope_del(doc, pos, op->components[i].num);
          break;
        default:
          return 1;
      }
    }
  } else {
    if (op->content.type == TEXT_OP_INSERT) {
      rope_insert(doc, op->skip, str_content(&op->content.str));
    } else if (op->content.type == TEXT_OP_DELETE) {
      rope_del(doc, op->skip, op->content.num);
    }
  }
  return 0;
}

int text_cursor_check(const rope *doc, text_cursor cursor) {
  size_t len = rope_char_count(doc);
  return cursor.start > len || cursor.end > len;
}

static size_t transform_position(size_t cursor, const text_op *op) {
  if (op->components) {
    size_t pos = 0;
    // I could actually use the op_iter stuff above - but I think its simpler like this.
    for (int i = 0; i < op->num_components && cursor > pos; i++) {
      switch (op->components[i].type) {
        case TEXT_OP_SKIP:
          if (cursor <= pos + op->components[i].num) {
            return cursor;
          }
          pos += op->components[i].num;
          break;
        case TEXT_OP_INSERT: {
          size_t len = str_num_chars(&op->components[i].str);
          pos += len;
          cursor += len;
          break;
        }
        case TEXT_OP_DELETE:
          cursor -= MIN(op->components[i].num, cursor - pos);
          break;
        default: break;
      }
    }
    return cursor;
  } else {
    // Tiny op, owned by someone else.
    switch (op->content.type) {
      case TEXT_OP_INSERT:
        return cursor <= op->skip ? cursor : cursor + str_num_chars(&op->content.str);
      case TEXT_OP_DELETE:
        return cursor <= op->skip ? cursor : cursor - MIN(op->content.num, cursor - op->skip);
      default: return cursor;
    }
  }
}

text_cursor text_op_transform_cursor(text_cursor cursor, const text_op *op, bool is_own_op) {
  if (is_own_op) {
    size_t pos = 0;
    if (op->components) {
      // Just track the position. We'll teleport the cursor to the end anyway.
      for (int i = 0; i < op->num_components; i++) {
        switch (op->components[i].type) {
            // We're guaranteed that a valid operation won't end in a skip.
          case TEXT_OP_SKIP:
            pos += op->components[i].num;
            break;
          case TEXT_OP_INSERT:
            pos += str_num_chars(&op->components[i].str);
            break;
          default: // Just eat deletes.
            break;
        }
      }
    } else {
      if (is_own_op) {
        switch (op->content.type) {
          case TEXT_OP_INSERT: pos = op->skip + str_num_chars(&op->content.str); break;
          case TEXT_OP_DELETE: pos = op->skip; break;
          default:     return cursor;
        }
      }
    }
    return text_cursor_make(pos, pos);
  } else {
    return text_cursor_make(transform_position(cursor.start, op), transform_position(cursor.end, op));
  }
}

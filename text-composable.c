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
  op->capacity = op->num_components = num;
  for (int i = 0; i < num; i++) {
    op->components[i] = components[i];
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

static text_op *ensure_capacity(text_op *op) {
  if (op->num_components == op->capacity) {
    op->capacity *= 2;
    op = realloc(op, sizeof(text_op) * op->capacity * sizeof(text_op_component));
  }
  return op;
}

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

static text_op *append(text_op *op, text_op_component c) {
  if (c.type == NONE
      || ((c.type == SKIP || c.type == DELETE) && c.num == 0)
      || (c.type == INSERT && !c.str.mem && c.str.num_chars == 0)) {
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


/*


# Makes a function for appending components to a given op.
# Exported for the randomOpGenerator.
exports._makeAppend = makeAppend = (op) -> (component) ->
  if component == 0 || component.i == '' || component.d == ''
    return
  else if op.length == 0
    op.push component
  else if typeof(component) == 'number' && typeof(op[op.length - 1]) == 'number'
    op[op.length - 1] += component
  else if component.i? && op[op.length - 1].i?
    op[op.length - 1].i += component.i
  else if component.d? && op[op.length - 1].d?
    op[op.length - 1].d += component.d
  else
    op.push component
  
#  checkOp op

# Makes 2 functions for taking components from the start of an op, and for peeking
# at the next op that could be taken.
makeTake = (op) ->
  # The index of the next component to take
  idx = 0
  # The offset into the component
  offset = 0

  # Take up to length n from the front of op. If n is null, take the next
  # op component. If indivisableField == 'd', delete components won't be separated.
  # If indivisableField == 'i', insert components won't be separated.
  take = (n, indivisableField) ->
    return null if idx == op.length
    #assert.notStrictEqual op.length, i, 'The op is too short to traverse the document'

    if typeof(op[idx]) == 'number'
      if !n? or op[idx] - offset <= n
        c = op[idx] - offset
        ++idx; offset = 0
        c
      else
        offset += n
        n
    else
      # Take from the string
      field = if op[idx].i then 'i' else 'd'
      c = {}
      if !n? or op[idx][field].length - offset <= n or field == indivisableField
        c[field] = op[idx][field][offset..]
        ++idx; offset = 0
      else
        c[field] = op[idx][field][offset...(offset + n)]
        offset += n
      c
  
  peekType = () ->
    op[idx]
  
  [take, peekType]

# Find and return the length of an op component
componentLength = (component) ->
  if typeof(component) == 'number'
    component
  else if component.i?
    component.i.length
  else
    component.d.length

# Normalize an op, removing all empty skips and empty inserts / deletes. Concatenate
# adjacent inserts and deletes.
exports.normalize = (op) ->
  newOp = []
  append = makeAppend newOp
  append component for component in op
  newOp

# Apply the op to the string. Returns the new string.
exports.apply = (str, op) ->
  p "Applying #{i op} to '#{str}'"
  throw new Error('Snapshot should be a string') unless typeof(str) == 'string'
  checkOp op

  pos = 0
  newDoc = []

  for component in op
    if typeof(component) == 'number'
      throw new Error('The op is too long for this document') if component > str.length
      newDoc.push str[...component]
      str = str[component..]
    else if component.i?
      newDoc.push component.i
    else
      throw new Error("The deleted text '#{component.d}' doesn't match the next characters in the document '#{str[...component.d.length]}'") unless component.d == str[...component.d.length]
      str = str[component.d.length..]
  
  throw new Error("The applied op doesn't traverse the entire document") unless '' == str

  newDoc.join ''

# transform op1 by op2. Return transformed version of op1.
# op1 and op2 are unchanged by transform.
exports.transform = (op, otherOp, side) ->
  throw new Error "side (#{side} must be 'left' or 'right'" unless side == 'left' or side == 'right'

  checkOp op
  checkOp otherOp
  newOp = []

  append = makeAppend newOp
  [take, peek] = makeTake op

  for component in otherOp
    if typeof(component) == 'number' # Skip
      length = component
      while length > 0
        chunk = take(length, 'i')
        throw new Error('The op traverses more elements than the document has') unless chunk != null

        append chunk
        length -= componentLength chunk unless typeof(chunk) == 'object' && chunk.i?
    else if component.i? # Insert
      if side == 'left'
        # The left insert should go first.
        o = peek()
        append take() if o?.i

      # Otherwise, skip the inserted text.
      append(component.i.length)
    else # Delete.
      #assert.ok component.d
      length = component.d.length
      while length > 0
        chunk = take(length, 'i')
        throw new Error('The op traverses more elements than the document has') unless chunk != null

        if typeof(chunk) == 'number'
          length -= chunk
        else if chunk.i?
          append(chunk)
        else
          #assert.ok chunk.d
          # The delete is unnecessary now.
          length -= chunk.d.length
  
  # Append extras from op1
  while (component = take())
    throw new Error "Remaining fragments in the op: #{i component}" unless component?.i?
    append component

  newOp


# Compose 2 ops into 1 op.
exports.compose = (op1, op2) ->
  p "COMPOSE #{i op1} + #{i op2}"
  checkOp op1
  checkOp op2

  result = []

  append = makeAppend result
  [take, _] = makeTake op1

  for component in op2
    if typeof(component) == 'number' # Skip
      length = component
      while length > 0
        chunk = take(length, 'd')
        throw new Error('The op traverses more elements than the document has') unless chunk != null

        append chunk
        length -= componentLength chunk unless typeof(chunk) == 'object' && chunk.d?

    else if component.i? # Insert
      append {i:component.i}

    else # Delete
      offset = 0
      while offset < component.d.length
        chunk = take(component.d.length - offset, 'd')
        throw new Error('The op traverses more elements than the document has') unless chunk != null

        # If its delete, append it. If its skip, drop it and decrease length. If its insert, check the strings match, drop it and decrease length.
        if typeof(chunk) == 'number'
          append {d:component.d[offset...(offset + chunk)]}
          offset += chunk
        else if chunk.i?
          throw new Error("The deleted text doesn't match the inserted text") unless component.d[offset...(offset + chunk.i.length)] == chunk.i
          offset += chunk.i.length
          # The ops cancel each other out.
        else
          # Delete
          append chunk
    
  # Append extras from op1
  while (component = take())
    throw new Error "Trailing stuff in op1 #{i component}" unless component?.d?
    append component

  result
  

invertComponent = (c) ->
  if typeof(c) == 'number'
    c
  else if c.i?
    {d:c.i}
  else
    {i:c.d}

# Invert an op
exports.invert = (op) ->
  result = []
  append = makeAppend result

  append(invertComponent component) for component in op
  
  result

if window?
  window.ot ||= {}
  window.ot.types ||= {}
  window.ot.types.text = exports

*/
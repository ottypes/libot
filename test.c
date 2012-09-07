// Tests for the composable text implementation.

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "text-composable.h"

void sanity() {
  text_op_component insert = {INSERT};
  str_init2(&insert.str, (uint8_t *)"hi there");
  text_op *op = text_op_from_components(&insert, 1);
  
  text_doc *doc = rope_new();
  text_op_apply(doc, op);
  
  uint8_t *str = rope_createcstr(doc, NULL);

  assert(strcmp((char *)str, "hi there") == 0);

  free(str);
  
  text_op_free(op);
  rope_free(doc);
}


// A selection of different unicode characters to pick from.
// As far as I can tell, there are no unicode characters assigned which
// take up more than 4 bytes in utf-8.
static const char *UCHARS[] = {
  "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "1", "2", "3", " ", "\n", // ASCII
  "©", "¥", "½", // The Latin-1 suppliment (U+80 - U+ff)
  "Ύ", "Δ", "δ", "Ϡ", // Greek (U+0370 - U+03FF)
  "←", "↯", "↻", "⇈", // Arrows (U+2190 – U+21FF)
  "𐆐", "𐆔", "𐆘", "𐆚", // Ancient roman symbols (U+10190 – U+101CF)
};

// s is the size of the buffer, including the \0. This function might use
// fewer bytes than that.
void random_string(uint8_t *buffer, size_t s) {
  if (s == 0) { return; }
  uint8_t *pos = buffer;
  
  while(1) {
    uint8_t *c = (uint8_t *)UCHARS[random() % (sizeof(UCHARS) / sizeof(UCHARS[0]))];
    
    size_t bytes = strlen((char *)c);
    
    size_t remaining_space = buffer + s - pos - 1;
    
    if (remaining_space < bytes) {
      break;
    }
    
    memcpy(pos, c, bytes);
    pos += bytes;
  }
  
  *pos = '\0';
}

static float rand_float() {
  return (float)random() / INT32_MAX;
}

text_op *random_op(rope *doc) {
  uint8_t buffer[100];
  
  size_t remaining_chars = rope_char_count(doc);
  
  float p = 0.99f;
  
  text_op_component components[10];
  int num_components = 0;
  
  while (num_components < 10 && rand_float() < p) {
    // First a skip (probably).
    if (remaining_chars && rand_float() < 0.9f) {
      components[num_components].type = SKIP;
      size_t len = random() % remaining_chars;
      components[num_components].num = len;
      remaining_chars -= len;
      num_components++;
    }
    
    if (remaining_chars == 0 || rand_float() < 0.7) {
      // Insert.
      size_t l = 1 + random() % 9;
      l *= l; // random number from 1 to 10, squared. Small inserts are much more frequent than large ones.
      random_string(buffer, l);
      components[num_components].type = INSERT;
      str_init2(&components[num_components].str, buffer);
      num_components++;
    } else {
      // Delete.
      components[num_components].type = DELETE;
      size_t len = random() % remaining_chars;
      components[num_components].num = len;
      remaining_chars -= len;
      num_components++;
    }
    
    p *= 0.4;
  }
  return text_op_from_components(components, num_components);
}

void random_op_test() {
  srandom(1);
  rope *doc = rope_new();
  for (int i = 0; i < 100000; i++) {
    text_op *op1 = random_op(doc);
    text_op *op2 = random_op(doc);
    
    assert(text_op_check(doc, op1) == 0);
    
    text_op *op1_ = text_op_transform(op1, op2, true);
    text_op *op2_ = text_op_transform(op2, op1, false);
    
    rope *doc2 = rope_copy(doc);

    text_op_apply(doc, op1);
    text_op_apply(doc, op2_);
    
    text_op_apply(doc2, op2);
    text_op_apply(doc2, op1_);
    
    uint8_t *doc1_str = rope_createcstr(doc, NULL);
    uint8_t *doc2_str = rope_createcstr(doc2, NULL);
    
    assert(strcmp((char *)doc1_str, (char *)doc2_str) == 0);
    free(doc1_str);
    free(doc2_str);
    
    rope_free(doc2);
    text_op_free(op1);
    text_op_free(op2);
    text_op_free(op1_);
    text_op_free(op2_);
  }
  rope_free(doc);
}

int main() {
  sanity();
  random_op_test();
  
  return 0;
}

// Tests for the composable text implementation.

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <assert.h>
#include "text.h"
#include "str.h"

void sanity() {
  text_op_component insert = {TEXT_OP_INSERT};
  str_init2(&insert.str, (uint8_t *)"hi there");
  text_op op = text_op_from_components(&insert, 1);
  
  rope *doc = rope_new();
  text_op_apply(doc, &op);
  
  uint8_t *str = rope_create_cstr(doc);

  assert(strcmp((char *)str, "hi there") == 0);

  free(str);
  
  text_op_free(&op);
  rope_free(doc);
}

void left_hand_inserts() {
  text_op ins1 = text_op_insert(100, (uint8_t *)"abc");
  text_op ins2 = text_op_insert(100, (uint8_t *)"def");
  
  text_op ins1_ = text_op_transform(&ins1, &ins2, false);
  assert(ins1_.skip == 103);
  
  text_op ins2_ = text_op_transform(&ins2, &ins1, true);
  assert(ins2_.skip == 100);
//  assert(ins2_.components[0].num == 100);
}

// A selection of different unicode characters to pick from.
// As far as I can tell, there are no unicode characters assigned which
// take up more than 4 bytes in utf-8.
static const char *UCHARS[] = {
  "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "1", "2", "3", " ", "\n", // ASCII
//  "©", "¥", "½", // The Latin-1 suppliment (U+80 - U+ff)
//  "Ύ", "Δ", "δ", "Ϡ", // Greek (U+0370 - U+03FF)
//  "←", "↯", "↻", "⇈", // Arrows (U+2190 – U+21FF)
//  "𐆐", "𐆔", "𐆘", "𐆚", // Ancient roman symbols (U+10190 – U+101CF)
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

text_op random_op(rope *doc) {
  uint8_t buffer[100];
  
  size_t remaining_chars = rope_char_count(doc);
  
  float p = 0.99f;
  
  text_op_component components[10];
  int num_components = 0;
  
  while (num_components < 10 && rand_float() < p) {
    // First a skip (probably).
    if (remaining_chars && rand_float() < 0.9f) {
      components[num_components].type = TEXT_OP_SKIP;
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
      components[num_components].type = TEXT_OP_INSERT;
      str_init2(&components[num_components].str, buffer);
      num_components++;
    } else {
      // Delete.
      components[num_components].type = TEXT_OP_DELETE;
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
  srandom(2);
  rope *doc = rope_new();
  for (int i = 0; i < 100000; i++) {
    text_op op1 = random_op(doc);
    text_op op2 = random_op(doc);
    
//    printf("\n---- ops\n");
//    text_op_print(&op1);
//    text_op_print(&op2);
    
    assert(text_op_check(doc, &op1) == 0);
    assert(text_op_check(doc, &op2) == 0);
    
    text_op op1_ = text_op_transform(&op1, &op2, true);
    text_op op2_ = text_op_transform(&op2, &op1, false);
    
//    printf("\n---- transformed ops\n");
//    text_op_print(&op1_);
//    text_op_print(&op2_);
    
    rope *doc2 = rope_copy(doc);
    rope *doc3 = rope_copy(doc);
    rope *doc4 = rope_copy(doc);
    
    text_op_apply(doc, &op1);
    assert(text_op_check(doc, &op2_) == 0);
    text_op_apply(doc, &op2_);
    
    text_op_apply(doc2, &op2);
    assert(text_op_check(doc2, &op1_) == 0);
    text_op_apply(doc2, &op1_);
    
    uint8_t *doc1_str = rope_create_cstr(doc);
    uint8_t *doc2_str = rope_create_cstr(doc2);
    
    assert(strcmp((char *)doc1_str, (char *)doc2_str) == 0);
    
    // Compose
    
    text_op op12 = text_op_compose(&op1, &op2_);
    assert(text_op_check(doc3, &op12) == 0);
    text_op op21 = text_op_compose(&op2, &op1_);
    assert(text_op_check(doc4, &op21) == 0);

//    printf("\n---- composed ops\n");
//    text_op_print(&op12);
//    text_op_print(&op21);
    
    text_op_apply(doc3, &op12);
    text_op_apply(doc4, &op21);

    uint8_t *doc3_str = rope_create_cstr(doc3);
    uint8_t *doc4_str = rope_create_cstr(doc4);
    
    assert(strcmp((char *)doc3_str, (char *)doc4_str) == 0);
    assert(strcmp((char *)doc1_str, (char *)doc3_str) == 0);
    
    free(doc1_str);
    free(doc2_str);
    free(doc3_str);
    free(doc4_str);
    
    rope_free(doc2);
    rope_free(doc3);
    rope_free(doc4);
    
    text_op_free(&op1);
    text_op_free(&op2);
    text_op_free(&op1_);
    text_op_free(&op2_);
    text_op_free(&op12);
    text_op_free(&op21);
  }
  rope_free(doc);
}

typedef struct {
  void *bytes;
  size_t num;
  size_t capacity;
} buffer;

void append(void *bytes, size_t num, void *buf_) {
  buffer *buf = (buffer *)buf_;
  
  while (num + buf->num >= buf->capacity) {
    if (buf->capacity) {
      buf->capacity *= 2;
    } else {
      buf->capacity = 16;
    }
    buf->bytes = realloc(buf->bytes, buf->capacity);
  }
  memcpy(&buf->bytes[buf->num], bytes, num);
  buf->num += num;
}

void serialize_deserialze() {
  srandom(5);
  rope *doc = rope_new_with_utf8((uint8_t *)"Hi there!! OMG strings rock.");
  
  buffer buf = {};
  for (int i = 0; i < 100000; i++) {
    text_op op = random_op(doc);
    
    buf.num = 0;
    text_op_to_bytes(&op, append, &buf);
    
    text_op op_copy;
    ssize_t size = text_op_from_bytes(&op_copy, buf.bytes, buf.num);
    assert(size > 0);
    assert(size == buf.num);
    
    // Now op and op_copy should be the same.
    rope *doc2 = rope_copy(doc);
    
    text_op_apply(doc, &op);
    text_op_apply(doc2, &op_copy);
    
    uint8_t *doc_str = rope_create_cstr(doc);
    uint8_t *doc2_str = rope_create_cstr(doc2);
    
    assert(strcmp((char *)doc_str, (char *)doc2_str) == 0);
    
    // Clean up.
    free(doc_str);
    free(doc2_str);
    text_op_free(&op);
    text_op_free(&op_copy);
    rope_free(doc2);
  }
  rope_free(doc);
  free(buf.bytes);
}

static void test_cursor(text_op *op, bool is_own,
                    size_t start, size_t end, size_t e_start, size_t e_end) {
  text_cursor result = text_op_transform_cursor(text_cursor_make(start, end), op, is_own);
  assert(result.start == e_start);
  assert(result.end == e_end);
}

void transform_cursor() {
  text_op ins = text_op_insert(10, (uint8_t *)"oh hi");
  text_op del = text_op_delete(25, 20);
  text_op op = text_op_compose(&ins, &del);
  // The op skips 10, inserts 5 characters, skips another 10 then deletes 20 characters.
  
#define C(s, e) text_cursor_make(s, e)
  // A cursor at the start of the inserted text shouldn't move.
  test_cursor(&op, false, 10, 10, 10, 10);
  test_cursor(&op, false, 10, 11, 10, 16);
  
  // Unless its your cursor.
  test_cursor(&ins, true, 10, 11, 15, 15);
  
  // Any character inside the deleted region should move to the start of the region.
  test_cursor(&del, false, 25, 40, 25, 25);
  test_cursor(&del, false, 35, 50, 25, 30);
  test_cursor(&del, false, 45, 60, 25, 40);

  test_cursor(&del, true, 25, 40, 25, 25);
  test_cursor(&del, true, 35, 50, 25, 25);
  test_cursor(&del, true, 45, 60, 25, 25);

  // Cursors before the deleted region are uneffected
  test_cursor(&del, false, 10, 25, 10, 25);
  
  // Cursors past the end of the deleted region get pulled back.
  test_cursor(&del, false, 55, 60, 35, 40);
  
  // Your cursor always teleports to the end of the last insert or the deletion site.
  test_cursor(&ins, true,   0, 50, 15, 15);
  test_cursor(&ins, true, 100, 50, 15, 15);
  test_cursor(&del, true,   0, 50, 25, 25);
  test_cursor(&del, true, 100, 50, 25, 25);

  // More complicated cases
  test_cursor(&op, false, 0, 5, 0, 5);
  test_cursor(&op, false, 100, 5, 85, 5);
  test_cursor(&op, false, 10, 5, 10, 5);
  test_cursor(&op, false, 11, 5, 16, 5);

  test_cursor(&op, false, 20, 5, 25, 5);
  test_cursor(&op, false, 30, 5, 25, 5);
  test_cursor(&op, false, 40, 5, 25, 5);
  test_cursor(&op, false, 41, 5, 26, 5);

  test_cursor(&op, true, 0, 100, 25, 25);
#undef C

  text_op_free(&ins);
  text_op_free(&del);
  text_op_free(&op);
}

void benchmark_string() {
  printf("Benchmarking string copy\n");
  
//  long iterations = 2000000000;
  long iterations = 20000000;
  
  str s1;
  str_init2(&s1, (uint8_t *)"Hi there this string is longer than 16 bytes");
  
  struct timeval start, end;
  
  gettimeofday(&start, NULL);
  
  for (long i = 0; i < iterations; i++) {
    str s2;
    str_init_with_copy(&s2, &s1);
    str_destroy(&s2);
  }
  
  gettimeofday(&end, NULL);
  
  double elapsedTime = end.tv_sec - start.tv_sec;
  elapsedTime += (end.tv_usec - start.tv_usec) / 1e6;
  printf("did %ld iterations in %f ms: %f Miter/sec\n",
         iterations, elapsedTime * 1000, iterations / elapsedTime / 1000000);
}

void benchmark_apply() {
  printf("Benchmarking apply...\n");
  
  long iterations = 20000000;
  
  struct timeval start, end;
  
  // Make the test stable
  srandom(1234);
  
  int doclens[] = {100, 1000, 10000, 100000, 1000000};

  for (int dl = 0; dl < sizeof(doclens) / sizeof(doclens[0]); dl++) {
    int doclen = doclens[dl];
    
    rope *doc = rope_new();
    for (int i = 0; i < doclen; i++) {
      rope_insert(doc, 0, (uint8_t *)"a");
    }

    text_op ops[1000];
    for (int i = 0; i < 1000; i++) {
      text_op_component c[2] = {{TEXT_OP_SKIP}};
      c[0].num = random() % doclen + 1;
      
      if (i % 2) {
        c[1].type = TEXT_OP_INSERT;
        str_init2(&c[1].str, (uint8_t *)"x");
      } else {
        c[1].type = TEXT_OP_DELETE;
        c[1].num = 1;
      }
      text_op_from_components2(&ops[i], c, 2);
    }
    
    for (int t = 0; t < 1; t++) {
      printf("doclen %d run %d\n", doclen, t);
      gettimeofday(&start, NULL);
      
      for (long i = 0; i < iterations; i++) {
        text_op_apply(doc, &ops[i % 1000]);
      }
      
      gettimeofday(&end, NULL);
      
      double elapsedTime = end.tv_sec - start.tv_sec;
      elapsedTime += (end.tv_usec - start.tv_usec) / 1e6;
      printf("%ld iterations in %f ms: %f Miter/sec\n",
             iterations, elapsedTime * 1000, iterations / elapsedTime / 1000000);
    }
    
    for (unsigned int i = 0; i < 1000; i++) {
      text_op_free(&ops[i]);
    }
    rope_free(doc);
  }
}

void benchmark_transform() {
  printf("Benchmarking transform...\n");
  
  long iterations = 200000000;
  
  struct timeval start, end;
  
  // Make the test stable
  srandom(1234);
  
  int doclen = 10000;
  
  text_op ops[1000];
  for (int i = 0; i < 1000; i++) {
    text_op_component c[2] = {{TEXT_OP_SKIP}};
    c[0].num = random() % doclen + 1;
    
    if (i % 2) {
      c[1].type = TEXT_OP_INSERT;
      str_init2(&c[1].str, (uint8_t *)"x");
    } else {
      c[1].type = TEXT_OP_DELETE;
      c[1].num = 1;
    }
    text_op_from_components2(&ops[i], c, 2);
  }

  text_op_component c[2] = {{TEXT_OP_SKIP}, {TEXT_OP_DELETE}};
  c[0].num = doclen / 2;
  c[1].num = 1;
  text_op op = text_op_from_components(c, 2);
  
  for (int t = 0; t < 2; t++) {
    gettimeofday(&start, NULL);

    for (long i = 0; i < iterations; i++) {
      text_op op_ = text_op_transform(&op, &ops[i % 1000], true);
      text_op_free(&op);
      op = op_;
    }
  
    gettimeofday(&end, NULL);
    printf("run %d\n", t);
  
    double elapsedTime = end.tv_sec - start.tv_sec;
    elapsedTime += (end.tv_usec - start.tv_usec) / 1e6;
    printf("dl %d did %ld iterations in %f ms: %f Miter/sec\n",
           doclen, iterations, elapsedTime * 1000, iterations / elapsedTime / 1000000);
  }

  for (int i = 0; i < 1000; i++) {
    text_op_free(&ops[i]);
  }
  text_op_free(&op);
}

int main() {
  sanity();
  left_hand_inserts();
  serialize_deserialze();
  transform_cursor();
  
  random_op_test();
  
//  benchmark_string();
  
  benchmark_apply();
  benchmark_transform();
  return 0;
}

//
//  test.c
//  OT
//
//  Created by Joseph Gentle on 31/08/12.
//  Copyright (c) 2012 Joseph Gentle. All rights reserved.
//

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
}

int main() {
  sanity();
  
  return 0;
}

//
//  utf8.h
//  OT
//
//  Created by Joseph Gentle on 2/09/12.
//  Copyright (c) 2012 Joseph Gentle. All rights reserved.
//

#ifndef OT_utf8_h
#define OT_utf8_h

#include <stdint.h>
#include <stddef.h>

// Count the characters in a utf8 string.
size_t strlen_utf8(const uint8_t *_s);

// This little function counts how many bytes a certain number of characters take up.
uint8_t *count_utf8_chars(const uint8_t *str, size_t num_chars);

#endif

//
//  utf8.c
//  OT
//
//  Created by Joseph Gentle on 2/09/12.
//  Copyright (c) 2012 Joseph Gentle. All rights reserved.
//
#include "utf8.h"

#define ONEMASK ((size_t)(-1) / 0xFF)

// From http://www.daemonology.net/blog/2008-06-05-faster-utf8-strlen.html
size_t strlen_utf8(const uint8_t *_s)
{
	const uint8_t *s;
	size_t count = 0;
	size_t u;
	unsigned char b;
  
	/* Handle any initial misaligned bytes. */
	for (s = _s; (uintptr_t)(s) & (sizeof(size_t) - 1); s++) {
		b = *s;
    
		/* Exit if we hit a zero byte. */
		if (b == '\0')
			goto done;
    
		/* Is this byte NOT the first byte of a character? */
		count += (b >> 7) & ((~b) >> 6);
	}
  
	/* Handle complete blocks. */
	for (; ; s += sizeof(size_t)) {
		/* Prefetch 256 bytes ahead. */
		__builtin_prefetch(&s[256], 0, 0);
    
		/* Grab 4 or 8 bytes of UTF-8 data. */
		u = *(size_t *)(s);
    
		/* Exit the loop if there are any zero bytes. */
		if ((u - ONEMASK) & (~u) & (ONEMASK * 0x80))
			break;
    
		/* Count bytes which are NOT the first byte of a character. */
		u = ((u & (ONEMASK * 0x80)) >> 7) & ((~u) >> 6);
		count += (u * ONEMASK) >> ((sizeof(size_t) - 1) * 8);
	}
  
	/* Take care of any left-over bytes. */
	for (; ; s++) {
		b = *s;
    
		/* Exit if we hit a zero byte. */
		if (b == '\0')
			break;
    
		/* Is this byte NOT the first byte of a character? */
		count += (b >> 7) & ((~b) >> 6);
	}
  
done:
	return ((s - _s) - count);
}

static inline size_t codepoint_size(uint8_t byte) {
  if (byte <= 0x7f) { return 1; } // 0x74 = 0111 1111
  else if (byte <= 0xbf) { return SIZE_MAX; } // 1011 1111. Invalid for a starting byte.
  else if (byte <= 0xdf) { return 2; } // 1101 1111
  else if (byte <= 0xef) { return 3; } // 1110 1111
  else if (byte <= 0xf7) { return 4; } // 1111 0111
  else if (byte <= 0xfb) { return 5; } // 1111 1011
  else if (byte <= 0xfd) { return 6; } // 1111 1101
  else { return SIZE_MAX; }
}

// This little function counts how many bytes a certain number of characters take up.
// Obviously, not optimized like the function above is.
uint8_t *count_utf8_chars(const uint8_t *str, size_t num_chars) {
  // Const is kinda gross. Discard qualifiers.
  uint8_t *p = (uint8_t *)str;
  for (unsigned int i = 0; i < num_chars && *p; i++) {
    p += codepoint_size(*p);
  }
  return p;
}

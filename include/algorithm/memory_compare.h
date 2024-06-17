#pragma once 

#include "stdint.h"
#include "string.h"

/* Given two uint8_t pointers a, b, of lengths as, bs, determine the 
   greatest segment of shared values. Result is returned as a packed 
   uint32_t value, hence only up to 255 bytes of data can be compared.
   [31 ... 24] <- Unused. 
   [23 ... 16] <- Index where overlapping segment of a begin. 
   [15 ...  8] <- Index where overlapping segment of b begin. 
   [ 7 ...  0] <- Max shared bytes. */
uint32_t 
max_shared_vlaues(uint8_t *a, size_t as, uint8_t *b, size_t bs);
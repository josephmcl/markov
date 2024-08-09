#include "algorithm/memory_compare.h"


uint32_t 
max_shared_vlaues(uint8_t *a, size_t as, uint8_t *b, size_t bs) {
    
    uint32_t max  = 0;                
    uint32_t maxi = 0;
    uint32_t maxj = 0;
    uint8_t *ap, *bp, *tmp, *spa, *spb;
    size_t length = 0;

    for (size_t i = 0; i < as; ++i) {
        
        ap = a + i;
        
        for (size_t j = 0; j < bs; ++j) {
            
            bp = b + j;
            
            tmp = memchr(bp, *ap, bs - j);
            if (tmp == NULL) 
                break;

            j += tmp - bp;
            bp = tmp;

            length = 0;

            for (size_t k = 0; i + k < as && j + k < bs; ++k) {
                
                spa = ap + k;
                spb = bp + k;
                
                if (*spa != *spb) 
                    break;
                
                length += 1;
            } 
            if (length > max) {

                max = length;
                maxi = i;
                maxj = j;
            }
        }
    }

    /* Pack result */
    return (maxi << 16) + (maxj << 8) + (max);
}
/*
 * malloc_wrap.c
 *
 *  Created on: 13 июл. 2026 г.
 *      Author: DANIL
 */


#include <string.h>
#include "FreeRTOS.h"

void *__wrap_malloc(size_t n)  { return pvPortMalloc(n); }
void  __wrap_free(void *p)     { vPortFree(p); }
void *__wrap_calloc(size_t n, size_t s) {
    void *p = pvPortMalloc(n * s);
    if (p) memset(p, 0, n * s);
    return p;
}

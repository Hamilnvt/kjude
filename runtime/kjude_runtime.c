#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

void int_print(int32_t n) { printf("%d\n", n); }

void *c_malloc(int32_t size)
{
    assert(size > 0);
    return malloc((size_t)size);
}

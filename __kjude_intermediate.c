#include <stdint.h>

int32_t double_int(int32_t a, int32_t b(void));
int32_t main(void);

int32_t double_int(int32_t a, int32_t b(void)) {
return 2 * a;
}
int main(void) {
int32_t x = double_int(5);
x = double_int(x);
}

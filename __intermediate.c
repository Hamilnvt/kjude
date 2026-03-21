#include <stdint.h>

int main(void) {
    void foo(void) {
        print("yeah");
    }
    void main(void) {
        foo();
    }
    return 0;
}

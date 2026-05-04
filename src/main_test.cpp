#include <iostream>
#include <cstdio>
int main() {
    fprintf(stderr, "TEST_MAIN_START\n");
    fflush(stderr);
    fprintf(stderr, "TEST_MAIN_END\n");
    return 0;
}
'TEST_EOF'
gcc -o /home/dongbai/test_main /home/dongbai/seckill-system/src/main_test.cpp 2>&1
/home/dongbai/test_main 2>&1
echo "Exit: $?"

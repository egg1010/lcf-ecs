// tmp_size_check.cpp - 检查 manager 大小
#include "include/component.hpp"
#include <cstdio>

int main()
{
    fprintf(stderr, "sizeof(ecs::manager) = %zu\n", sizeof(ecs::manager));
    return 0;
}

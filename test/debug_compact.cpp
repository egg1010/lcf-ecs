#include "include/part/class_pool.hpp"
#include "include/part/class_pool_views.hpp"
#include <iostream>

int main()
{
    class_pool<int> sp;
    for (int i = 0; i < 100; ++i) { sp.emplace_back(i); }
    sp.sparse_erase_at(10);
    sp.sparse_erase_at(20);
    sp.sparse_erase_at(30);

    std::cout << "size=" << sp.size() << " count=" << sp.count() << " is_dense=" << sp.is_dense() << "\n";

    int dst[100] = {};
    size_t n = compact_to(sp, dst, 100);
    std::cout << "compact_to returned n=" << n << "\n";

    std::cout << "dst[0]=" << dst[0] << " (expected 0)\n";
    std::cout << "dst[9]=" << dst[9] << " (expected 9)\n";
    std::cout << "dst[10]=" << dst[10] << " (expected 11)\n";
    std::cout << "dst[11]=" << dst[11] << " (expected 12)\n";
    std::cout << "dst[19]=" << dst[19] << " (expected 19)\n";
    std::cout << "dst[20]=" << dst[20] << " (expected 21)\n";
    std::cout << "dst[21]=" << dst[21] << " (expected 22)\n";

    // 用 begin/end 遍历检查
    std::cout << "\nbegin/end 遍历:\n";
    size_t idx = 0;
    for (auto it = sp.begin(); it != sp.end() && idx < 25; ++it, ++idx)
    {
        std::cout << "  [" << idx << "] = " << *it << "\n";
    }
    return 0;
}

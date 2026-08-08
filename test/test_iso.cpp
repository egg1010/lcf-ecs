#include "include/part/utf8pp/utf8pp.hpp"
#include <iostream>
int main(){
    utf8pp s("Hello世界World");
    std::cout << "find 世界 = " << s.find("世界") << "\n";
    std::cout << "find(string_view) = " << s.find(std::string_view("世界")) << "\n";
    std::cout << "find(世界,6) = " << s.find("世界", 6) << "\n";
    utf8pp empty;
    std::cout << "empty.find(a) start\n";
    size_t r = empty.find("a");
    std::cout << "empty.find(a) = " << r << " npos=" << utf8pp::npos << "\n";
    return 0;
}

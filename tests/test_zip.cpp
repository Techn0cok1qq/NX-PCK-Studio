// cd tests && g++ -std=c++17 -I../source -o test_zip test_zip.cpp -lz && ./test_zip x.zip   (x.zip には png が入っている想定)
#include "zipread.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
int main(int c, char** v) {
    std::ifstream f(v[1], std::ios::binary);
    std::vector<uint8_t> z{std::istreambuf_iterator<char>(f), {}};
    std::vector<zr::Named> o; zr::readPngs(z, o);
    printf("count=%d read=%zu\n", zr::countPng(z), o.size());
    for (auto& n : o) printf("%s %zu\n", n.name.c_str(), n.data.size());
}

// PCで PCK 生成部だけを検証する: ./test_pck skin.png cape.png out.pck [be]
#include "../source/pck.hpp"
#include <fstream>
#include <iterator>
static std::vector<uint8_t> rd(const char* p){ std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
int main(int c, char** v){
    pck::SkinParams p; p.name = "テスト Skin 😀"; p.theme = "My Theme"; p.skinId = 123456; p.packId = 4242; p.slim = true;
    p.little = !(c > 4 && std::string(v[4]) == "be");
    p.skinPng = rd(v[1]); p.capePng = rd(v[2]);
    auto o = pck::makeSkinPack(p);
    std::ofstream(v[3], std::ios::binary).write((const char*)o.data(), o.size());
    printf("wrote %zu bytes\n", o.size());
}

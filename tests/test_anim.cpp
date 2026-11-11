// PCで GIF / PNG / 合成部分を検証する: ./test_anim in.gif template.png outdir
// outdir/f<N>_full.rgba (GIFの合成済みフレーム W*H*4), cape<N>.png, meta.txt を出力
#include "../source/animcape.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
static std::vector<uint8_t> rd(const char* p){ std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
static void wr(const std::string& p, const std::vector<uint8_t>& d){ std::ofstream(p, std::ios::binary).write((const char*)d.data(), d.size()); }
int main(int c, char** v){
    if (c < 4) return 2;
    std::vector<uint8_t> gif = rd(v[1]), tplRgba = rd(v[2]);   // template は raw RGBA 384x192
    std::string out = v[3];
    int n = 0, W = 0, H = 0;
    int cnt = anim::decodeGif(gif, [&](const uint8_t* px, int w, int h){
        wr(out + "/f" + std::to_string(n) + "_full.rgba", std::vector<uint8_t>(px, px + (size_t)w * h * 4));
        std::vector<uint8_t> frame;
        anim::encodeFrame(tplRgba, 384, 192, px, w, h, frame);
        auto fp = anim::encodePng(frame.data(), 384, 192);
        wr(out + "/frame" + std::to_string(n) + ".png", fp);
        auto cape = anim::capeTexture(frame, 384, 192);
        wr(out + "/cape" + std::to_string(n) + ".png", anim::encodePng(cape.data(), 384, 192));
        n++; return true; }, &W, &H);
    printf("frames=%d size=%dx%d\n", cnt, W, H);
}

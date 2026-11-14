// cd tests && g++ -std=c++17 -I../source -o test_discord test_discord.cpp && ./test_discord
#include <cassert>
#include <cstdio>
#include "dccore.hpp"
using namespace dccore;
int main() {
    // 判定
    assert(classify("a.png", 64, 64) == SKIN);
    assert(classify("my_cape.png", 64, 64) == SKIN);        // 大きさが決め手 (64x64 のマントは無い)
    assert(classify("Cape.PNG", 64, 32) == CAPE);
    assert(classify("赤いマント.png", 64, 32) == CAPE);
    assert(classify("skin.png", 64, 32) == SKIN);
    assert(classify("青のスキン.png", 64, 32) == SKIN);
    assert(classify("image.png", 64, 32) == BOTH);
    assert(classify("cape_skin.png", 64, 32) == BOTH);       // 両方入っていれば決められない
    assert(classify("a.png", 128, 128) == SKIN);             // 大きなスキン (展開図) は取り込む。使うときに 64 へ縮小する
    assert(classify("a.png", 256, 256) == SKIN);
    assert(classify("a.png", 512, 512) == SKIN);
    assert(classify("a.png", 128, 64) == SKIN);              // 64x32 形式の大きなもの
    assert(classify("a.png", 512, 256) == SKIN);
    assert(classify("my_cape.png", 128, 64) == SKIP);        // 大きなマントは使えない
    assert(classify("cape_skin.png", 256, 128) == SKIN);
    assert(classify("a.png", 1024, 1024) == SKIP);           // 512 まで
    assert(classify("a.png", 192, 192) == SKIP);             // 64 の 2 のべき乗倍だけ
    assert(classify("a.png", 256, 64) == SKIP);
    assert(isSkinSize(64, 64) && isSkinSize(64, 32) && isSkinSize(512, 512) && isSkinSize(256, 128));
    assert(!isSkinSize(64, 48) && !isSkinSize(512, 128) && !isSkinSize(0, 0));
    assert(classify("a.png", 64, 48) == SKIP);
    assert(classify("a.png", 22, 17) == SKIP);

    // 添付の取り出し (応答は新しい順 → 古い順に並ぶ)
    std::string js = R"([
      {"id":"1003","attachments":[{"id":"2002","filename":"my cape.PNG","size":148,"url":"https://cdn.discordapp.com/a/2002/cape.PNG?ex=1&is=2&hm=3","content_type":"image/png"}]},
      {"id":"1002","attachments":[{"id":"2001","filename":"skin.png","size":208,"url":"https://cdn.discordapp.com/a/2001/skin.png?ex=1&is=2&hm=3"},
                                   {"id":"2003","filename":"スキン.png","size":300,"url":"https://cdn.discordapp.com/a/2003/x.png"}]},
      {"id":"1001","attachments":[{"id":"2000","filename":"notes.txt","size":10,"url":"https://cdn.discordapp.com/a/notes.txt","content_type":"text/plain"}]},
      {"id":"1000","content":"hi","attachments":[]},
      {"id":"999"}
    ])";
    std::vector<Att> a;
    assert(parseMessages(js, a));
    assert(a.size() == 4);
    assert(a[0].id == "2000" && !looksPng(a[0]));            // 古い順
    assert(a[1].id == "2001" && looksPng(a[1]) && a[1].size == 208);
    assert(a[1].url == "https://cdn.discordapp.com/a/2001/skin.png?ex=1&is=2&hm=3");   // & が & に戻る
    assert(a[2].id == "2003" && a[2].name == "スキン.png");
    assert(a[3].id == "2002" && looksPng(a[3]));
    Att t; t.name = "noext"; t.type = "image/png"; assert(looksPng(t));
    t.name = "x.jpg"; t.type = "image/jpeg"; assert(!looksPng(t));

    // GIF / ZIP の添付 (アニメーションスキン用)
    Att g; g.name = "Anim.GIF"; assert(looksGif(g) && !looksPng(g) && !looksZip(g));
    g.name = "x"; g.type = "image/gif"; assert(looksGif(g));
    Att zp; zp.name = "skins.ZIP"; assert(looksZip(zp) && !looksGif(zp));
    zp.name = "x"; zp.type = "application/x-zip-compressed"; assert(looksZip(zp));
    assert(isGifData("GIF89a....") && !isGifData("<html>...") && isZipData(std::string("PK\x03\x04") + std::string(30, 'x')) && !isZipData("hello"));

    // 壊れた / 想定外の応答
    assert(!parseMessages(R"({"message":"401: Unauthorized","code":0})", a));
    assert(!parseMessages("<html>", a));
    assert(parseMessages("[]", a) && a.empty());
    puts("ok");
}

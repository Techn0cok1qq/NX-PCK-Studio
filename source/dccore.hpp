// Discord 同期の純粋なロジック (メッセージJSONから添付を取り出す / スキンかマントかを判定する)
// libnx にも curl にも依存しないので、PC 上で tests/test_discord.cpp からテストできます。
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "updcore.hpp"

namespace dccore {

struct Att { std::string id, name, url, type; size_t size = 0; };

inline std::string lowerAscii(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');   // 日本語(UTF-8)のバイトは触らない
    return s;
}
inline bool contains(const std::string& s, const char* w) { return s.find(w) != std::string::npos; }

// PNG らしい添付か (拡張子 .png か content_type が image/png)
inline bool looksPng(const Att& a) {
    std::string n = lowerAscii(a.name);
    if (n.size() > 4 && n.compare(n.size() - 4, 4, ".png") == 0) return true;
    return lowerAscii(a.type) == "image/png";
}

// GET /channels/{id}/messages の応答 (新しい順の配列) から添付を取り出す。古い順に並べ替えて返す
inline bool parseMessages(const std::string& json, std::vector<Att>& out) {
    updcore::Json j;
    if (!updcore::parseJson(json, j) || j.t != updcore::Json::ARR) return false;
    out.clear();
    for (size_t mi = j.a.size(); mi-- > 0;) {
        const updcore::Json* as = j.a[mi].get("attachments");
        if (!as || as->t != updcore::Json::ARR) continue;
        for (auto& a : as->a) {
            Att t;
            t.id = a.str("id"); t.name = a.str("filename"); t.url = a.str("url"); t.type = a.str("content_type");
            t.size = (size_t)a.num("size");
            if (t.id.empty() || t.url.empty()) continue;
            out.push_back(t);
        }
    }
    return true;
}

enum Kind { SKIN, CAPE, BOTH, SKIP };
// 大きさと名前で振り分ける
//  64x64                              → スキン
//  64x32 でファイル名に cape / マント → マント
//  64x32 でファイル名に skin / スキン → スキン
//  64x32 でどちらとも言えない         → BOTH (古い形式のスキンにも、マントにもなるので両方に入れる)
//  それ以外の大きさ                   → SKIP (このアプリでは使えない)
inline Kind classify(const std::string& name, uint32_t w, uint32_t h) {
    if (w != 64) return SKIP;
    if (h == 64) return SKIN;
    if (h != 32) return SKIP;
    std::string n = lowerAscii(name);
    bool cape = contains(n, "cape") || contains(n, "マント");
    bool skin = contains(n, "skin") || contains(n, "スキン");
    if (cape && !skin) return CAPE;
    if (skin && !cape) return SKIN;
    return BOTH;
}

}  // namespace dccore

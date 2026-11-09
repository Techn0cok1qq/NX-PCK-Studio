// PCK / LOC writer. PCK-studio-Web (GPL-3.0) の encode / buildLocBuffer / addSkinToPackage の移植。
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace pck {

struct Prop  { std::string key, value; };
struct Asset { std::string name; uint32_t type = 0; std::vector<Prop> props; std::vector<uint8_t> data; };

// UTF-8 -> UTF-16 (JSの charCodeAt と同じ単位で長さを数える)
inline std::u16string toUtf16(const std::string& s) {
    std::u16string o;
    for (size_t i = 0; i < s.size();) {
        uint32_t c = (uint8_t)s[i];
        int n = 0;
        if (c < 0x80) n = 0;
        else if ((c >> 5) == 6)  { c &= 0x1F; n = 1; }
        else if ((c >> 4) == 14) { c &= 0x0F; n = 2; }
        else if ((c >> 3) == 30) { c &= 0x07; n = 3; }
        else { i++; continue; }
        i++;
        for (int k = 0; k < n && i < s.size(); k++, i++) c = (c << 6) | ((uint8_t)s[i] & 0x3F);
        if (c >= 0x10000) { c -= 0x10000; o.push_back((char16_t)(0xD800 + (c >> 10))); o.push_back((char16_t)(0xDC00 + (c & 0x3FF))); }
        else o.push_back((char16_t)c);
    }
    return o;
}

struct Out {
    std::vector<uint8_t> b;
    bool le;
    explicit Out(bool little) : le(little) {}
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) { if (le) { u8(v & 255); u8(v >> 8); } else { u8(v >> 8); u8(v & 255); } }
    void u32(uint32_t v) {
        if (le) for (int i = 0; i < 4; i++) u8((v >> (8 * i)) & 255);
        else    for (int i = 3; i >= 0; i--) u8((v >> (8 * i)) & 255);
    }
    void bytes(const std::vector<uint8_t>& d) { b.insert(b.end(), d.begin(), d.end()); }
    // PCK用: 文字数(u32) + UTF-16 + 0(u32)
    void str16(const std::string& s) {
        std::u16string u = toUtf16(s);
        u32((uint32_t)u.size());
        for (char16_t c : u) u16(c);
        u32(0);
    }
    // LOC用: バイト数(u16, BE) + UTF-8
    void str8(const std::string& s) {
        size_t n = s.size() > 65535 ? 65535 : s.size();
        u16((uint16_t)n);
        b.insert(b.end(), s.begin(), s.begin() + n);
    }
};

// ---- PCK本体 ----
inline std::vector<uint8_t> build(const std::vector<Asset>& assets, uint32_t pckType, bool little) {
    Out o(little);
    o.u32(pckType);

    std::vector<std::string> keys;  // 出現順に一意化
    auto keyIndex = [&](const std::string& k) -> uint32_t {
        for (size_t i = 0; i < keys.size(); i++) if (keys[i] == k) return (uint32_t)i;
        keys.push_back(k);
        return (uint32_t)keys.size() - 1;
    };
    for (const Asset& a : assets) for (const Prop& p : a.props) keyIndex(p.key);

    o.u32((uint32_t)keys.size());
    for (size_t i = 0; i < keys.size(); i++) { o.u32((uint32_t)i); o.str16(keys[i]); }

    o.u32((uint32_t)assets.size());
    for (const Asset& a : assets) { o.u32((uint32_t)a.data.size()); o.u32(a.type); o.str16(a.name); }

    for (const Asset& a : assets) {
        o.u32((uint32_t)a.props.size());
        for (const Prop& p : a.props) { o.u32(keyIndex(p.key)); o.str16(p.value); }
        o.bytes(a.data);
    }
    return o.b;
}

// ---- localisation.loc (常にビッグエンディアン / UTF-8) ----
inline std::vector<uint8_t> buildLoc(const std::vector<std::pair<std::string, std::string>>& entries) {
    static const char* langs[] = {
        "cs-CS", "cs-CZ", "da-DA", "da-DK", "de-DE", "el-EL", "el-GR", "en-EN", "en-GB", "es-ES", "es-MX",
        "fi-FI", "fr-FR", "it-IT", "ja-JP", "ko-KR", "la-LAS", "nb-NO", "nl-NL", "no-NO", "pl-PL", "pt-BR",
        "pt-PT", "ru-RU", "sk-SK", "sv-SE", "sv-SV", "tr-TR", "zh-CHT", "zh-CN", "zh-HANS", "zh-HANT"};
    const uint32_t nl = sizeof(langs) / sizeof(langs[0]);

    Out h(false);
    h.u32(2);
    h.u32(nl);
    h.u8(0);                       // useUniqueIds = false
    h.u32((uint32_t)entries.size());
    for (auto& kv : entries) h.str8(kv.first);

    std::vector<std::vector<uint8_t>> bodies;
    for (uint32_t i = 0; i < nl; i++) {
        Out e(false);
        e.u32(2);                  // version
        e.u8(0);                   // hasUids
        e.str8(langs[i]);
        e.u32((uint32_t)entries.size());
        for (auto& kv : entries) e.str8(kv.second);
        h.str8(langs[i]);
        h.u32((uint32_t)e.b.size());
        bodies.push_back(e.b);
    }
    for (auto& b : bodies) h.bytes(b);
    return h.b;
}


// ---- PCK / LOC の読み込み (取り込み・ファイルを開く用) ----
struct Reader {
    const uint8_t* p; size_t n, i = 0; bool le, ok = true;
    Reader(const std::vector<uint8_t>& d, bool little) : p(d.data()), n(d.size()), le(little) {}
    uint8_t u8() { if (i + 1 > n) { ok = false; return 0; } return p[i++]; }
    uint16_t u16() { if (i + 2 > n) { ok = false; return 0; } uint16_t v = le ? (uint16_t)(p[i] | (p[i + 1] << 8)) : (uint16_t)((p[i] << 8) | p[i + 1]); i += 2; return v; }
    uint32_t u32() {
        if (i + 4 > n) { ok = false; return 0; }
        uint32_t v = 0;
        if (le) for (int k = 3; k >= 0; k--) v = (v << 8) | p[i + k];
        else for (int k = 0; k < 4; k++) v = (v << 8) | p[i + k];
        i += 4; return v;
    }
    static void put8(std::string& o, uint32_t c) {
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
        else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    }
    // 文字数(u32) + UTF-16 + 0(u32)
    std::string str16() {
        uint32_t len = u32();
        if (!ok || len > 100000 || i + (size_t)len * 2 + 4 > n) { ok = false; return ""; }
        std::string o;
        for (uint32_t k = 0; k < len; k++) {
            uint32_t c = u16();
            if (c >= 0xD800 && c < 0xDC00 && k + 1 < len) { uint32_t d = u16(); k++; c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); }
            put8(o, c);
        }
        u32();
        return o;
    }
    // バイト数(u16) + UTF-8
    std::string str8() {
        uint32_t len = u16();
        if (!ok || i + len > n) { ok = false; return ""; }
        std::string o((const char*)p + i, len); i += len; return o;
    }
};

inline bool parseWith(const std::vector<uint8_t>& d, bool little, std::vector<Asset>& out, uint32_t& type) {
    Reader r(d, little);
    type = r.u32();
    if (type > 255) return false;
    uint32_t nk = r.u32();
    if (!r.ok || nk > 10000) return false;
    std::vector<std::pair<uint32_t, std::string>> keys;
    bool xml = false;
    for (uint32_t k = 0; k < nk && r.ok; k++) { uint32_t idx = r.u32(); std::string s = r.str16(); if (s == "XMLVERSION") xml = true; keys.push_back({idx, s}); }
    if (!r.ok) return false;
    if (xml) r.u32();
    uint32_t na = r.u32();
    if (!r.ok || na > 100000) return false;
    struct H { uint32_t size, type; std::string name; };
    std::vector<H> hs;
    for (uint32_t k = 0; k < na && r.ok; k++) { H h; h.size = r.u32(); h.type = r.u32(); h.name = r.str16(); hs.push_back(h); }
    if (!r.ok) return false;
    out.clear();
    for (auto& h : hs) {
        Asset a; a.name = h.name; a.type = h.type;
        uint32_t np = r.u32();
        if (!r.ok || np > 10000) return false;
        for (uint32_t k = 0; k < np; k++) {
            uint32_t ki = r.u32(); std::string v = r.str16();
            if (!r.ok) return false;
            std::string key;
            for (auto& e : keys) if (e.first == ki) { key = e.second; break; }
            a.props.push_back({key, v});
        }
        if (!r.ok || r.i + h.size > r.n) return false;
        a.data.assign(d.begin() + r.i, d.begin() + r.i + h.size);
        r.i += h.size;
        out.push_back(std::move(a));
    }
    return true;
}
// リトルエンディアン → ビッグエンディアンの順に試す
inline bool parse(const std::vector<uint8_t>& d, std::vector<Asset>& out, uint32_t& type, bool& little) {
    if (parseWith(d, true, out, type)) { little = true; return true; }
    if (parseWith(d, false, out, type)) { little = false; return true; }
    return false;
}
// localisation.loc → (キー, 文字列) 。英語(無ければ先頭の言語)を使う
inline bool parseLoc(const std::vector<uint8_t>& d, std::vector<std::pair<std::string, std::string>>& out) {
    out.clear();
    Reader r(d, false);
    r.u32();
    uint32_t nl = r.u32();
    uint8_t uids = r.u8();
    if (!r.ok || uids || nl == 0 || nl > 100) return false;
    uint32_t ne = r.u32();
    if (!r.ok || ne > 100000) return false;
    std::vector<std::string> keys;
    for (uint32_t k = 0; k < ne && r.ok; k++) keys.push_back(r.str8());
    std::vector<std::string> langs;
    for (uint32_t l = 0; l < nl && r.ok; l++) { langs.push_back(r.str8()); r.u32(); }
    if (!r.ok) return false;
    int want = 0;
    for (uint32_t l = 0; l < nl; l++) if (langs[l] == "en-EN" || langs[l] == "en-GB") { want = (int)l; break; }
    for (uint32_t l = 0; l < nl && r.ok; l++) {
        r.u32(); r.u8(); r.str8();
        uint32_t c = r.u32();
        if (!r.ok || c != ne) return false;
        std::vector<std::string> vals;
        for (uint32_t k = 0; k < c && r.ok; k++) vals.push_back(r.str8());
        if (!r.ok) return false;
        if ((int)l == want) for (uint32_t k = 0; k < ne; k++) out.push_back({keys[k], vals[k]});
    }
    return true;
}

// ---- PNG ヘッダの幅・高さ ----
inline bool pngSize(const std::vector<uint8_t>& d, uint32_t& w, uint32_t& h) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (d.size() < 24 || memcmp(d.data(), sig, 8) != 0) return false;
    w = ((uint32_t)d[16] << 24) | ((uint32_t)d[17] << 16) | ((uint32_t)d[18] << 8) | d[19];
    h = ((uint32_t)d[20] << 24) | ((uint32_t)d[21] << 16) | ((uint32_t)d[22] << 8) | d[23];
    return true;
}

inline std::string pad8(uint32_t v) { char b[16]; snprintf(b, sizeof b, "%08u", v); return b; }

// ---- スキン1枚(+マント任意)の PCK ----
struct SkinParams {
    std::string name, theme;
    uint32_t packId = 1, skinId = 100000;
    bool slim = false;
    bool little = true;      // Switch = little endian
    uint32_t pckType = 8;    // Webツールの「新規パッケージ」と同じ
    std::vector<uint8_t> skinPng, capePng;
};

inline std::vector<uint8_t> makeSkinPack(const SkinParams& p) {
    const std::string id = pad8(p.skinId);
    const std::string nameKey = "IDS_dlcskin" + id + "_DISPLAYNAME";
    const std::string themeKey = "IDS_dlcskin" + id + "_THEMENAME";

    std::vector<Asset> a;

    Asset root; root.name = "0"; root.type = 4;
    root.props.push_back({"PACKID", std::to_string(p.packId)});
    a.push_back(root);

    std::vector<std::pair<std::string, std::string>> loc;
    loc.push_back({"IDS_DISPLAY_NAME", p.name});
    loc.push_back({nameKey, p.name});
    if (!p.theme.empty()) loc.push_back({themeKey, p.theme});
    Asset l; l.name = "localisation.loc"; l.type = 6; l.data = buildLoc(loc);
    a.push_back(l);

    const std::string capeName = "dlccape" + id + ".png";
    if (!p.capePng.empty()) {
        Asset c; c.name = capeName; c.type = 1; c.data = p.capePng;
        a.push_back(c);
    }

    Asset s; s.name = "dlcskin" + id + ".png"; s.type = 0; s.data = p.skinPng;
    s.props.push_back({"DISPLAYNAME", p.name});
    s.props.push_back({"DISPLAYNAMEID", nameKey});
    if (!p.theme.empty()) {
        s.props.push_back({"THEMENAME", p.theme});
        s.props.push_back({"THEMENAMEID", themeKey});
    }
    s.props.push_back({"ANIM", p.slim ? "0x00080000" : "0x00040000"});
    s.props.push_back({"GAME_FLAGS", "0x18"});
    s.props.push_back({"FREE", "1"});
    if (!p.capePng.empty()) s.props.push_back({"CAPEPATH", capeName});
    a.push_back(s);

    return build(a, p.pckType, p.little);
}

}  // namespace pck

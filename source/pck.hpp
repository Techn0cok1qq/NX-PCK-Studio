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

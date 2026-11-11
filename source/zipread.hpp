// 最小限の ZIP 読み込み (無圧縮 / deflate のみ・ZIP64 と暗号化は非対応)
// アニメーションスキンで「ZIP に入った連番スキン PNG」を読むために使う。SDL / libnx に依存しない
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>

namespace zr {

struct Entry { std::string name; uint16_t method = 0; uint32_t csize = 0, usize = 0, off = 0; };
struct Named { std::string name; std::vector<uint8_t> data; };

inline uint16_t r16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t r32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// 中央ディレクトリを読む。ZIP でなければ false
inline bool list(const std::vector<uint8_t>& z, std::vector<Entry>& out) {
    out.clear();
    if (z.size() < 22) return false;
    size_t lo = z.size() > 22 + 65535 ? z.size() - 22 - 65535 : 0, e = std::string::npos;
    for (size_t i = z.size() - 22 + 1; i-- > lo;) if (r32(&z[i]) == 0x06054b50) { e = i; break; }
    if (e == std::string::npos) return false;
    uint32_t n = r16(&z[e + 10]), cdOff = r32(&z[e + 16]);
    size_t p = cdOff;
    for (uint32_t k = 0; k < n; k++) {
        if (p + 46 > z.size() || r32(&z[p]) != 0x02014b50) return false;
        uint16_t flags = r16(&z[p + 8]), nl = r16(&z[p + 28]), el = r16(&z[p + 30]), cl = r16(&z[p + 32]);
        if (p + 46 + nl > z.size()) return false;
        Entry en;
        en.method = r16(&z[p + 10]); en.csize = r32(&z[p + 20]); en.usize = r32(&z[p + 24]); en.off = r32(&z[p + 42]);
        en.name.assign((const char*)&z[p + 46], nl);
        bool zip64 = en.csize == 0xFFFFFFFFu || en.usize == 0xFFFFFFFFu || en.off == 0xFFFFFFFFu;
        if (!(flags & 1) && !zip64 && (en.method == 0 || en.method == 8)) out.push_back(en);   // 暗号化 / ZIP64 / 未対応の圧縮は飛ばす
        p += 46 + nl + el + cl;
    }
    return true;
}

inline bool extract(const std::vector<uint8_t>& z, const Entry& e, std::vector<uint8_t>& out, size_t maxOut = 8u << 20) {
    out.clear();
    if (e.usize > maxOut || (size_t)e.off + 30 > z.size() || r32(&z[e.off]) != 0x04034b50) return false;
    size_t ds = (size_t)e.off + 30 + r16(&z[e.off + 26]) + r16(&z[e.off + 28]);
    if (ds + e.csize > z.size()) return false;
    if (e.method == 0) { out.assign(z.begin() + ds, z.begin() + ds + e.csize); return out.size() == e.usize; }
    out.resize(e.usize);
    z_stream s; memset(&s, 0, sizeof s);
    if (inflateInit2(&s, -15) != Z_OK) return false;
    s.next_in = (Bytef*)&z[ds]; s.avail_in = e.csize;
    s.next_out = out.data(); s.avail_out = (uInt)out.size();
    int rc = inflate(&s, Z_FINISH);
    inflateEnd(&s);
    return rc == Z_STREAM_END && s.total_out == e.usize;
}

inline std::string baseName(const std::string& n) { size_t p = n.find_last_of("/\\"); return p == std::string::npos ? n : n.substr(p + 1); }
inline bool isPngEntry(const Entry& e) {   // フォルダー / __MACOSX / ._ で始まる Mac のゴミを除いた .png
    if (e.name.empty() || e.name.back() == '/' || e.name.find("__MACOSX") != std::string::npos) return false;
    std::string b = baseName(e.name);
    if (b.size() < 5 || b.compare(0, 2, "._") == 0) return false;
    std::string x = b.substr(b.size() - 4);
    for (auto& c : x) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return x == ".png";
}
inline int countPng(const std::vector<uint8_t>& z) {
    std::vector<Entry> es;
    if (!list(z, es)) return 0;
    int n = 0;
    for (auto& e : es) if (isPngEntry(e)) n++;
    return n;
}
inline void readPngs(const std::vector<uint8_t>& z, std::vector<Named>& out) {
    std::vector<Entry> es;
    if (!list(z, es)) return;
    for (auto& e : es) {
        if (!isPngEntry(e)) continue;
        Named n; n.name = baseName(e.name);
        if (extract(z, e, n.data, 4u << 20)) out.push_back(std::move(n));
    }
}

}  // namespace zr

// アップデート確認の純粋なロジック (JSON解析 / バージョン比較 / リリース情報の取り出し)
// libnx にも curl にも依存しないので、PC 上で tests/test_update.cpp からテストできます。
#pragma once
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace updcore {

// ---------- 最小限の JSON ----------
struct Json {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;
    const Json* get(const std::string& k) const {
        if (t != OBJ) return nullptr;
        for (auto& kv : o) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    std::string str(const std::string& k) const { const Json* j = get(k); return j && j->t == STR ? j->s : ""; }
    double num(const std::string& k) const { const Json* j = get(k); return j && j->t == NUM ? j->n : 0; }
};

struct Parser {
    const std::string& s;
    size_t i = 0;
    int depth = 0;
    bool ok = true;
    explicit Parser(const std::string& str) : s(str) {}
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++; }
    static void utf8(std::string& o, uint32_t c) {
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
        else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    }
    bool hex4(uint32_t& v) {
        if (i + 4 > s.size()) return false;
        v = 0;
        for (int k = 0; k < 4; k++) {
            char c = s[i++]; v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return false;
        }
        return true;
    }
    bool str(std::string& out) {
        if (i >= s.size() || s[i] != '"') return false;
        i++;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case '/': case '\\': case '"': out += e; break;
            case 'u': {
                uint32_t v;
                if (!hex4(v)) return false;
                if (v >= 0xD800 && v < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                    size_t save = i; i += 2;
                    uint32_t lo;
                    if (hex4(lo) && lo >= 0xDC00 && lo < 0xE000) v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                    else i = save;
                }
                utf8(out, v);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool value(Json& j) {
        if (++depth > 64) return false;
        struct G { int& d; ~G() { d--; } } g{depth};
        ws();
        if (i >= s.size()) return false;
        char c = s[i];
        if (c == '{') {
            j.t = Json::OBJ; i++; ws();
            if (i < s.size() && s[i] == '}') { i++; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!str(k)) return false;
                ws();
                if (i >= s.size() || s[i++] != ':') return false;
                Json v;
                if (!value(v)) return false;
                j.o.push_back({k, std::move(v)});
                ws();
                if (i >= s.size()) return false;
                if (s[i] == ',') { i++; continue; }
                if (s[i] == '}') { i++; return true; }
                return false;
            }
        }
        if (c == '[') {
            j.t = Json::ARR; i++; ws();
            if (i < s.size() && s[i] == ']') { i++; return true; }
            for (;;) {
                Json v;
                if (!value(v)) return false;
                j.a.push_back(std::move(v));
                ws();
                if (i >= s.size()) return false;
                if (s[i] == ',') { i++; continue; }
                if (s[i] == ']') { i++; return true; }
                return false;
            }
        }
        if (c == '"') { j.t = Json::STR; return str(j.s); }
        if (s.compare(i, 4, "true") == 0) { j.t = Json::BOOL; j.b = true; i += 4; return true; }
        if (s.compare(i, 5, "false") == 0) { j.t = Json::BOOL; j.b = false; i += 5; return true; }
        if (s.compare(i, 4, "null") == 0) { j.t = Json::NUL; i += 4; return true; }
        char* end = nullptr;
        double d = strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) return false;
        j.t = Json::NUM; j.n = d; i = (size_t)(end - s.c_str());
        return true;
    }
};
inline bool parseJson(const std::string& text, Json& out) {
    Parser p(text);
    if (!p.value(out)) return false;
    p.ws();
    return p.i == text.size();
}

// ---------- バージョン ("v1.2.3" / "1.2" / "v1.2.3-beta" など) ----------
struct Ver { int v[3] = {0, 0, 0}; bool ok = false; };
inline Ver parseVersion(const std::string& s) {
    Ver r;
    size_t i = 0;
    while (i < s.size() && (s[i] == 'v' || s[i] == 'V' || s[i] == ' ')) i++;
    int idx = 0;
    bool any = false;
    while (i < s.size() && idx < 3) {
        if (s[i] < '0' || s[i] > '9') break;
        long n = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') { n = n * 10 + (s[i] - '0'); if (n > 100000000) n = 100000000; i++; }
        r.v[idx++] = (int)n; any = true;
        if (i < s.size() && s[i] == '.') i++; else break;
    }
    r.ok = any;
    return r;
}
// remote が local より新しければ true (読めないバージョンは新しいと見なさない)
inline bool isNewer(const std::string& remote, const std::string& local) {
    Ver a = parseVersion(remote), b = parseVersion(local);
    if (!a.ok || !b.ok) return false;
    for (int k = 0; k < 3; k++) { if (a.v[k] != b.v[k]) return a.v[k] > b.v[k]; }
    return false;
}

// ---------- GitHub Releases API の応答 ----------
struct Release {
    std::string tag, name, body, assetName, url, sha256;
    size_t size = 0;
    bool hasAsset = false;
};
inline bool endsWithNro(std::string n) {
    for (auto& c : n) c = (char)tolower((unsigned char)c);
    return n.size() > 4 && n.compare(n.size() - 4, 4, ".nro") == 0;
}
// 成功したら true。.nro のアセットが無ければ hasAsset=false
inline bool parseRelease(const std::string& json, Release& r) {
    Json j;
    if (!parseJson(json, j) || j.t != Json::OBJ) return false;
    r = Release();
    r.tag = j.str("tag_name");
    if (r.tag.empty()) return false;
    r.name = j.str("name");
    r.body = j.str("body");
    const Json* as = j.get("assets");
    if (as && as->t == Json::ARR) {
        const Json* best = nullptr;
        for (auto& a : as->a) {
            if (a.t != Json::OBJ || !endsWithNro(a.str("name")) || a.str("browser_download_url").empty()) continue;
            if (!best || a.str("name") == "nxpckstudio.nro") best = &a;
        }
        if (best) {
            r.assetName = best->str("name");
            r.url = best->str("browser_download_url");
            r.size = (size_t)best->num("size");
            std::string d = best->str("digest");  // "sha256:<64桁の16進>"
            if (d.compare(0, 7, "sha256:") == 0 && d.size() == 7 + 64) {
                r.sha256 = d.substr(7);
                for (auto& c : r.sha256) c = (char)tolower((unsigned char)c);
            }
            r.hasAsset = true;
        }
    }
    return true;
}

// リリースノートを画面に出せる長さに整える (Markdown の記号を軽く落とす)
inline std::string shortNotes(const std::string& body, size_t maxChars = 240, int maxLines = 6) {
    std::string out;
    int lines = 0;
    size_t pos = 0;
    while (pos <= body.size() && lines < maxLines) {
        size_t e = body.find('\n', pos);
        std::string ln = body.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
        size_t k = 0;
        while (k < ln.size() && (ln[k] == '#' || ln[k] == ' ')) k++;
        ln = ln.substr(k);
        if (ln.compare(0, 2, "- ") == 0 || ln.compare(0, 2, "* ") == 0) ln = "・" + ln.substr(2);
        if (!ln.empty()) { if (!out.empty()) out += '\n'; out += ln; lines++; }
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    if (out.size() > maxChars) {  // UTF-8 の文字境界で切る
        size_t cut = maxChars;
        while (cut > 0 && ((unsigned char)out[cut] & 0xC0) == 0x80) cut--;
        out = out.substr(0, cut) + "...";
    }
    return out;
}

}  // namespace updcore

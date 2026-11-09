// ファイル操作と設定
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <errno.h>

namespace util {

inline bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    size_t got = n > 0 ? fread(out.data(), 1, (size_t)n, f) : 0;
    fclose(f);
    return got == out.size();
}

inline void mkdirs(const std::string& path) {
    for (size_t i = 1; i < path.size(); i++) {
        if (path[i] == '/') {
            std::string d = path.substr(0, i);
            if (d.back() != ':') mkdir(d.c_str(), 0777);
        }
    }
    mkdir(path.c_str(), 0777);
}

// .tmp に書いてから rename (書き込み途中の壊れたファイルを読まれないように)
inline bool writeAtomic(const std::string& path, const std::vector<uint8_t>& data) {
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    size_t w = fwrite(data.data(), 1, data.size(), f);
    int c = fclose(f);
    if (w != data.size() || c != 0) { remove(tmp.c_str()); return false; }
    remove(path.c_str());  // FATでは上書きrenameが失敗することがある
    return rename(tmp.c_str(), path.c_str()) == 0;
}

// 英数字・._- 以外は _ にする
inline std::string sanitize(const std::string& s) {
    std::string o;
    for (unsigned char c : s) o += (isalnum(c) || c == '.' || c == '_' || c == '-') ? (char)c : '_';
    return o;
}

inline bool isPng(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s.size() > 4 && s.compare(s.size() - 4, 4, ".png") == 0;
}

inline std::string trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

struct Config {
    std::string token, channel;
    std::string folder = "MySkins";               // romfs/NX/<folder>/
    std::string titleId = "01006BD001E06800";
    bool little = true;                           // endian=little|big
};

inline const char* BASE = "sdmc:/switch/nxpckstudio/";

inline Config loadConfig() {
    Config c;
    std::string path = std::string(BASE) + "config.ini";
    FILE* f = fopen(path.c_str(), "r");
    if (!f) {
        mkdirs(BASE);
        f = fopen(path.c_str(), "w");
        if (f) {
            fputs("# romfs/NX/<folder>/ に .pck を置く (英数字)\nfolder=MySkins\n"
                  "titleId=01006BD001E06800\n"
                  "# little (Switch) / big\nendian=little\n", f);
            fclose(f);
        }
        return c;
    }
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        std::string s = trim(line);
        if (s.empty() || s[0] == '#') continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(s.substr(0, eq)), v = trim(s.substr(eq + 1));
        if (k == "token") c.token = v;
        else if (k == "channel") c.channel = v;
        else if (k == "folder" && !v.empty()) c.folder = sanitize(v);
        else if (k == "titleId" && !v.empty()) c.titleId = v;
        else if (k == "endian") c.little = (v != "big");
    }
    fclose(f);
    return c;
}

}  // namespace util

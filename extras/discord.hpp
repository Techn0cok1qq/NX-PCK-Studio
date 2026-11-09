// Discord チャンネルの添付PNGを inbox に保存する (REST: GET /channels/{id}/messages)
#pragma once
#include <curl/curl.h>
#include <string>
#include <vector>
#include "../source/util.hpp"
#include "../source/pck.hpp"

namespace discord {

static size_t onData(char* p, size_t s, size_t n, void* u) { ((std::string*)u)->append(p, s * n); return s * n; }

// bearer: Authorization ヘッダ (CDNダウンロードでは空にしてトークンを送らない)
inline bool get(const std::string& url, const std::string& auth, std::string& body, long& code) {
    CURL* c = curl_easy_init();
    if (!c) return false;
    struct curl_slist* h = nullptr;
    if (!auth.empty()) h = curl_slist_append(h, ("Authorization: " + auth).c_str());
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "DiscordBot (https://github.com/nxpckstudio, 0.1)");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
    // 証明書: SDに cacert.pem があれば検証する。無ければ検証なし(下のREADME参照)
    std::string ca = std::string(util::BASE) + "cacert.pem";
    if (FILE* f = fopen(ca.c_str(), "r")) { fclose(f); curl_easy_setopt(c, CURLOPT_CAINFO, ca.c_str()); }
    else { curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L); curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L); }
    CURLcode rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    return rc == CURLE_OK;
}

// JSON文字列を読む(最低限のエスケープのみ)。pos は開き引用符の直後
inline std::string readStr(const std::string& j, size_t pos) {
    std::string o;
    for (size_t i = pos; i < j.size(); i++) {
        char ch = j[i];
        if (ch == '"') break;
        if (ch == '\\' && i + 1 < j.size()) {
            char n = j[++i];
            if (n == 'u' && i + 4 < j.size()) { unsigned v = strtoul(j.substr(i + 1, 4).c_str(), nullptr, 16); o += v < 128 ? (char)v : '_'; i += 4; }
            else o += (n == 'n' ? '\n' : n);
        } else o += ch;
    }
    return o;
}

inline bool endsWithPng(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s.size() > 4 && s.compare(s.size() - 4, 4, ".png") == 0;
}

// 戻り値: 新しく保存した枚数 (-1 = 失敗, err に理由)
inline int sync(const util::Config& cfg, const std::string& inbox, std::string& err) {
    if (cfg.token.empty() || cfg.channel.empty()) { err = "config.ini に token と channel を設定してください"; return -1; }
    util::mkdirs(inbox);

    std::string body; long code = 0;
    if (!get("https://discord.com/api/v10/channels/" + cfg.channel + "/messages?limit=50", "Bot " + cfg.token, body, code)) {
        err = "Discordに接続できません (ネットワークを確認)"; return -1;
    }
    if (code == 401) { err = "トークンが違います (401)"; return -1; }
    if (code == 403) { err = "Botにチャンネルを読む権限がありません (403)"; return -1; }
    if (code == 404) { err = "チャンネルIDが違います (404)"; return -1; }
    if (code != 200) { err = "Discordエラー HTTP " + std::to_string(code); return -1; }

    int saved = 0;
    const std::string kFile = "\"filename\":\"", kUrl = "\"url\":\"", kId = "\"id\":\"";
    for (size_t p = body.find(kFile); p != std::string::npos; p = body.find(kFile, p + 1)) {
        size_t idPos = body.rfind(kId, p);                      // 添付の id は filename の直前にある
        size_t urlPos = body.find(kUrl, p);
        if (idPos == std::string::npos || urlPos == std::string::npos) continue;
        std::string attId = readStr(body, idPos + kId.size());
        std::string name = readStr(body, p + kFile.size());
        std::string url = readStr(body, urlPos + kUrl.size());
        if (!endsWithPng(name) || attId.empty() || url.empty()) continue;

        std::string dest = inbox + attId + "_" + util::sanitize(name);
        if (FILE* f = fopen(dest.c_str(), "rb")) { fclose(f); continue; }   // 取得済み

        std::string img; long ic = 0;
        if (!get(url, "", img, ic) || ic != 200) continue;                   // 添付URLは期限付きなのですぐ保存
        std::vector<uint8_t> data(img.begin(), img.end());
        uint32_t w, h;
        if (!pck::pngSize(data, w, h)) continue;
        if (util::writeAtomic(dest, data)) saved++;
    }
    return saved;
}

}  // namespace discord

// Discord のチャンネルに添付された PNG / GIF / ZIP を SD に保存する (Switch 専用: libnx + libcurl)
//  - Bot のトークンでチャンネルの最近のメッセージ(100件)を読み、PNG の添付を skins/ capes/ に保存します
//  - 通信は update.hpp と同じ設定 (HTTPS・証明書を検証) を使います。通信は別スレッドなので画面は止まりません
//  - 保存した添付の ID は discord_seen.txt に控えます (SD から消した画像を、次の同期でまた取ってくることはありません)
#pragma once
#include <atomic>
#include <cstdio>
#include <set>
#include <string>
#include <vector>
#include "update.hpp"
#include "dccore.hpp"
#include "pck.hpp"
#include "util.hpp"

namespace dc {

static std::atomic<int> st{upd::IDLE};          // upd::Phase を流用 (IDLE / RUNNING / DONE / FAILED)
static std::atomic<int> nowN{0}, totalN{0};     // 取得の進み具合 (枚数)
static std::atomic<bool> cancelFlag{false};
static std::string err;                          // FAILED の後で読む
static int nSkin = 0, nCape = 0, nBoth = 0, nSkip = 0, nFail = 0, nGif = 0, nZip = 0;   // DONE の後で読む
static std::string cfgToken, cfgChannel, dirSkin, dirCape, dirAnim;
static Thread th;
static bool thActive = false;

static const size_t MAX_PNG = 1u << 20;         // 1MB 超の PNG は取らない (スキンは 32KB 以下が普通)
static const size_t MAX_ANIM = 16u << 20;      // GIF / ZIP は 16MB まで

static std::string seenPath() { return std::string(util::BASE) + "discord_seen.txt"; }

struct Buf { std::string* s; size_t max; };
static size_t onBuf(char* p, size_t sz, size_t n, void* ud) {
    Buf* b = (Buf*)ud;
    if (b->s->size() + sz * n > b->max) return 0;
    b->s->append(p, sz * n);
    return sz * n;
}
static int onProg(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) { return cancelFlag ? 1 : 0; }

// auth が空でなければ Authorization ヘッダーを付ける (CDN からの画像取得では付けない)
static bool httpGet(const std::string& url, const std::string& auth, size_t maxBytes, std::string& body, long& code, std::string& e) {
    CURL* c = curl_easy_init();
    if (!c) { e = "通信を開始できません"; return false; }
    if (!upd::setupCurl(c, url, e)) { curl_easy_cleanup(c); return false; }
    struct curl_slist* h = nullptr;
    if (!auth.empty()) { h = curl_slist_append(h, ("Authorization: " + auth).c_str()); curl_easy_setopt(c, CURLOPT_HTTPHEADER, h); }
    Buf b{&body, maxBytes};
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onBuf);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProg);
    CURLcode rc = curl_easy_perform(c);
    code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (h) curl_slist_free_all(h);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) { e = upd::curlMsg(rc); return false; }
    return true;
}

static std::set<std::string> loadSeen() {
    std::set<std::string> s;
    std::vector<uint8_t> d;
    if (!util::readFile(seenPath(), d)) return s;
    std::string cur;
    for (uint8_t c : d) { if (c == '\n' || c == '\r') { if (!cur.empty()) s.insert(cur); cur.clear(); } else cur += (char)c; }
    if (!cur.empty()) s.insert(cur);
    return s;
}
static void markSeen(const std::string& id) {
    FILE* f = fopen(seenPath().c_str(), "ab");
    if (f) { fputs((id + "\n").c_str(), f); fclose(f); }
}

// 保存先のファイル名: 元の名前 (日本語はそのまま)。既にあれば末尾に添付IDの下6桁を付ける
static std::string destPathExt(const std::string& dir, const std::string& orig, const std::string& attId, const std::string& ext) {
    std::string name = util::cleanName(orig);
    if (name.empty() || name.size() > 120) name = "discord_" + attId + ext;
    if (!dccore::endsWith(dccore::lowerAscii(name), ext.c_str())) name += ext;
    struct stat sb;
    std::string path = dir + name;
    if (stat(path.c_str(), &sb) == 0) {
        std::string tail = attId.size() > 6 ? attId.substr(attId.size() - 6) : attId;
        path = dir + name.substr(0, name.size() - ext.size()) + "_" + tail + ext;
    }
    return path;
}
static std::string destPath(const std::string& dir, const std::string& orig, const std::string& attId) { return destPathExt(dir, orig, attId, ".png"); }

static void syncMain(void*) {
    std::string e;
    if (!upd::netReady(e)) { err = e; st = upd::FAILED; return; }

    std::string body; long code = 0;
    if (!httpGet("https://discord.com/api/v10/channels/" + cfgChannel + "/messages?limit=100", "Bot " + cfgToken, 4u << 20, body, code, e)) {
        err = cancelFlag ? "中止しました" : e; st = upd::FAILED; return;
    }
    if (code == 401) { err = "トークンが違います (401)。config.ini の token を確認してください"; st = upd::FAILED; return; }
    if (code == 403) { err = "Bot にこのチャンネルを読む権限がありません (403)。チャンネルを見る / メッセージ履歴を読む権限を付けてください"; st = upd::FAILED; return; }
    if (code == 404) { err = "チャンネルが見つかりません (404)。channel のIDと、Bot がサーバーに入っているかを確認してください"; st = upd::FAILED; return; }
    if (code == 429) { err = "Discord の確認回数の上限です。しばらくしてからもう一度お試しください"; st = upd::FAILED; return; }
    if (code != 200) { err = "Discord が応答しません (HTTP " + std::to_string(code) + ")"; st = upd::FAILED; return; }

    std::vector<dccore::Att> atts;
    if (!dccore::parseMessages(body, atts)) { err = "Discord の応答を読めません"; st = upd::FAILED; return; }

    std::set<std::string> seen = loadSeen();
    std::vector<const dccore::Att*> todo;
    for (auto& a : atts) {
        bool png = dccore::looksPng(a), gif = dccore::looksGif(a), zip = dccore::looksZip(a);
        if (seen.count(a.id) || !(png || gif || zip)) continue;
        if (a.size > (png ? MAX_PNG : MAX_ANIM)) { markSeen(a.id); nSkip++; continue; }
        todo.push_back(&a);
    }
    totalN = (int)todo.size();
    util::mkdirs(dirSkin); util::mkdirs(dirCape); util::mkdirs(dirAnim);

    for (const dccore::Att* a : todo) {
        if (cancelFlag) { err = "中止しました"; st = upd::FAILED; return; }
        std::string img, em; long ic = 0;
        bool isAnim = !dccore::looksPng(*a);   // GIF / ZIP
        bool ok = httpGet(a->url, "", isAnim ? MAX_ANIM : MAX_PNG, img, ic, em) && ic == 200;
        if (cancelFlag) { err = "中止しました"; st = upd::FAILED; return; }
        if (!ok) { nFail++; nowN++; continue; }     // 控えないので、次の同期でもう一度取りに行く
        if (isAnim) {   // GIF / ZIP はそのまま anim/ に保存する
            bool gif = dccore::looksGif(*a);
            if (gif ? !dccore::isGifData(img) : !dccore::isZipData(img)) { markSeen(a->id); nSkip++; nowN++; continue; }
            std::vector<uint8_t> raw(img.begin(), img.end());
            if (!util::writeAtomic(destPathExt(dirAnim, a->name, a->id, gif ? ".gif" : ".zip"), raw)) { nFail++; nowN++; continue; }
            markSeen(a->id);
            if (gif) nGif++; else nZip++;
            nowN++;
            continue;
        }
        std::vector<uint8_t> data(img.begin(), img.end());
        uint32_t w = 0, h = 0;
        dccore::Kind k = pck::pngSize(data, w, h) ? dccore::classify(a->name, w, h) : dccore::SKIP;
        bool saved = true;
        if (k == dccore::SKIN || k == dccore::BOTH) saved = util::writeAtomic(destPath(dirSkin, a->name, a->id), data) && saved;
        if (k == dccore::CAPE || k == dccore::BOTH) saved = util::writeAtomic(destPath(dirCape, a->name, a->id), data) && saved;
        if (!saved) { nFail++; nowN++; continue; }  // SD に書けなかった (空き容量など)
        markSeen(a->id);
        if (k == dccore::SKIN) nSkin++; else if (k == dccore::CAPE) nCape++; else if (k == dccore::BOTH) nBoth++; else nSkip++;
        nowN++;
    }
    st = upd::DONE;
}

// ---------- メインスレッドから使う API ----------
static void reap() {
    if (thActive && st != upd::RUNNING) { threadWaitForExit(&th); threadClose(&th); thActive = false; }
}
inline bool busy() { return st == upd::RUNNING; }
inline void cancel() { cancelFlag = true; }
inline bool cancelled() { return cancelFlag; }

// 開始できなければ false (理由は err)。update の通信と同時には動かせないので、呼ぶ前に upd::busy() が false になるのを待つこと
inline bool start(const std::string& token, const std::string& channel, const std::string& skinDir, const std::string& capeDir, const std::string& animDir) {
    if (busy()) return false;
    reap();
    std::string t = util::trim(token);
    if (t.compare(0, 4, "Bot ") == 0) t = util::trim(t.substr(4));   // "Bot " を付けて貼っても動くように
    std::string c = util::trim(channel);
    err.clear();
    nSkin = nCape = nBoth = nSkip = nFail = nGif = nZip = 0; nowN = 0; totalN = 0; cancelFlag = false;
    if (t.empty() || c.empty()) { err = "config.ini に token と channel を書いてください"; st = upd::FAILED; return false; }
    for (char ch : c) if (ch < '0' || ch > '9') { err = "channel はチャンネルIDの数字だけを書いてください"; st = upd::FAILED; return false; }
    cfgToken = t; cfgChannel = c; dirSkin = skinDir; dirCape = capeDir; dirAnim = animDir;
    st = upd::RUNNING;
    if (R_FAILED(threadCreate(&th, syncMain, nullptr, nullptr, 0x100000, 0x2C, -2))) { err = "通信スレッドを開始できません"; st = upd::FAILED; return false; }
    if (R_FAILED(threadStart(&th))) { threadClose(&th); err = "通信スレッドを開始できません"; st = upd::FAILED; return false; }
    thActive = true;
    return true;
}

// アプリ終了時: upd::shutdown() より先に呼ぶ (ソケットを閉じる前にスレッドを止める)
inline void shutdown() {
    cancelFlag = true;
    if (thActive) { threadWaitForExit(&th); threadClose(&th); thActive = false; }
}

}  // namespace dc

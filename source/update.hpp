// GitHub の最新リリースを見つけて、アプリ内だけで更新する (Switch 専用: libnx + libcurl)
//  - 確認 / ダウンロードは別スレッドで行うので、画面は止まりません
//  - 更新の適用は「アプリ終了後」に nro を差し替えて自分自身を起動し直します (動作中の nro は触りません)
#pragma once
#include <switch.h>
#include <curl/curl.h>
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>
#include <sys/stat.h>
#include "updcore.hpp"
#include "util.hpp"

namespace upd {

enum Phase { IDLE = 0, RUNNING = 1, DONE = 2, FAILED = 3 };

static std::atomic<int> chk{IDLE}, dl{IDLE};
static std::atomic<long long> dlNow{0}, dlTotal{0};
static std::atomic<bool> cancelFlag{false};
static updcore::Release rel;      // chk==DONE の後で読む
static bool isNewerRel = false;   // 同上
static std::string chkErr, dlErr; // FAILED の後で読む
static std::string repo, curVer, nroPath;
static Thread th;
static bool thActive = false, netUp = false, nifmUp = false, curlUp = false;
static bool downloadedOk = false; // 検証済みの nro が <nroPath>.new に置いてある

static std::string newPath() { return nroPath + ".new"; }
static std::string partPath() { return std::string(util::BASE) + "update.part"; }
static std::string logPath() { return std::string(util::BASE) + "update.log"; }

// argv[0] から今動いている nro のパスを決める
inline void init(const std::string& repoName, const std::string& version, int argc, char** argv) {
    repo = repoName; curVer = version;
    std::string p = (argc > 0 && argv && argv[0]) ? argv[0] : "";
    if (!p.empty() && p[0] == '/') p = "sdmc:" + p;
    if (p.size() < 5 || p.compare(p.size() - 4, 4, ".nro") != 0) p = "sdmc:/switch/nxpckstudio.nro";
    nroPath = p;
}

// ---------- ネットワーク ----------
static bool netReady(std::string& err) {
    if (!nifmUp) { if (R_FAILED(nifmInitialize(NifmServiceType_User))) { err = "ネットワークを使えません"; return false; } nifmUp = true; }
    NifmInternetConnectionType ty; u32 strength; NifmInternetConnectionStatus st;
    if (R_FAILED(nifmGetInternetConnectionStatus(&ty, &strength, &st)) || st != NifmInternetConnectionStatus_Connected) {
        err = "インターネットに接続されていません";
        return false;
    }
    if (!netUp) { if (R_FAILED(socketInitializeDefault())) { err = "ソケットを初期化できません"; return false; } netUp = true; }
    if (!curlUp) { if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) { err = "通信ライブラリを初期化できません"; return false; } curlUp = true; }
    return true;
}

static size_t onBody(char* p, size_t sz, size_t n, void* ud) {
    std::string* s = (std::string*)ud;
    if (s->size() + sz * n > (4u << 20)) return 0;  // 4MB 超は異常
    s->append(p, sz * n);
    return sz * n;
}
struct DlCtx { FILE* f; Sha256Context sha; size_t got; bool werr; };
static size_t onFile(char* p, size_t sz, size_t n, void* ud) {
    DlCtx* c = (DlCtx*)ud;
    size_t w = fwrite(p, 1, sz * n, c->f);
    if (w != sz * n) { c->werr = true; return 0; }
    sha256ContextUpdate(&c->sha, p, w);
    c->got += w;
    return w;
}
static int onProgress(void*, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    dlTotal = total; dlNow = now;
    return cancelFlag ? 1 : 0;
}

// 共通の curl 設定 (証明書は romfs の cacert.pem で検証する)
static std::string caPem;
static bool setupCurl(CURL* c, const std::string& url, std::string& err) {
    if (caPem.empty()) {
        std::vector<uint8_t> d;
        if (!util::readFile("romfs:/cacert.pem", d) || d.size() < 1000) { err = "証明書ファイル(cacert.pem)を読めません"; return false; }
        caPem.assign((const char*)d.data(), d.size());
    }
    curl_blob blob;
    blob.data = (void*)caPem.data(); blob.len = caPem.size(); blob.flags = CURL_BLOB_COPY;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &blob);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "NX-PCK-Studio-updater");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);   // 30秒間まったく進まなければ中止
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    return true;
}
static std::string curlMsg(CURLcode rc) {
    switch (rc) {
    case CURLE_COULDNT_RESOLVE_HOST: case CURLE_COULDNT_CONNECT: return "サーバーに接続できません";
    case CURLE_OPERATION_TIMEDOUT: return "通信がタイムアウトしました";
    case CURLE_PEER_FAILED_VERIFICATION: case CURLE_SSL_CACERT_BADFILE: case CURLE_SSL_CONNECT_ERROR:
        return "安全な接続を確認できません (本体の日時がずれていないか確認してください)";
    case CURLE_ABORTED_BY_CALLBACK: return "中止しました";
    default: return std::string("通信エラー (") + curl_easy_strerror(rc) + ")";
    }
}

// ---------- スレッド ----------
static void checkMain(void*) {
    std::string err;
    if (!netReady(err)) { chkErr = err; chk = FAILED; return; }
    CURL* c = curl_easy_init();
    if (!c) { chkErr = "通信を開始できません"; chk = FAILED; return; }
    std::string body;
    struct curl_slist* hdr = curl_slist_append(nullptr, "Accept: application/vnd.github+json");
    bool ok = setupCurl(c, "https://api.github.com/repos/" + repo + "/releases/latest", err);
    if (!ok) { curl_slist_free_all(hdr); curl_easy_cleanup(c); chkErr = err; chk = FAILED; return; }
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onBody);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);  // 終了時に素早く中断できるように
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) { chkErr = curlMsg(rc); chk = FAILED; return; }
    if (code == 404) { chkErr = "公開されたリリースがまだありません"; chk = FAILED; return; }
    if (code == 403 || code == 429) { chkErr = "GitHub の確認回数の上限です。しばらくしてからもう一度お試しください"; chk = FAILED; return; }
    if (code != 200) { chkErr = "GitHub が応答しません (HTTP " + std::to_string(code) + ")"; chk = FAILED; return; }
    updcore::Release r;
    if (!updcore::parseRelease(body, r)) { chkErr = "リリース情報を読めません"; chk = FAILED; return; }
    rel = r;
    isNewerRel = updcore::isNewer(r.tag, curVer);
    chk = DONE;
}

static void downloadMain(void*) {
    std::string err;
    downloadedOk = false;
    if (!rel.hasAsset) { dlErr = "このリリースに .nro がありません"; dl = FAILED; return; }
    if (!netReady(err)) { dlErr = err; dl = FAILED; return; }
    CURL* c = curl_easy_init();
    if (!c) { dlErr = "通信を開始できません"; dl = FAILED; return; }
    if (!setupCurl(c, rel.url, err)) { curl_easy_cleanup(c); dlErr = err; dl = FAILED; return; }
    remove(partPath().c_str());
    DlCtx ctx{fopen(partPath().c_str(), "wb"), {}, 0, false};
    if (!ctx.f) { curl_easy_cleanup(c); dlErr = "SDカードに書き込めません"; dl = FAILED; return; }
    sha256ContextCreate(&ctx.sha);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onFile);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    int cl = fclose(ctx.f);
    auto fail = [&](const std::string& m) { remove(partPath().c_str()); dlErr = m; dl = FAILED; };
    if (cancelFlag) { fail("中止しました"); return; }
    if (ctx.werr || cl != 0) { fail("SDカードに書き込めません (空き容量を確認してください)"); return; }
    if (rc != CURLE_OK) { fail(curlMsg(rc)); return; }
    // 検証: サイズ → NRO ヘッダー → SHA-256 (GitHub が digest を返したとき)
    if (rel.size && ctx.got != rel.size) { fail("ダウンロードしたサイズが合いません"); return; }
    {
        FILE* f = fopen(partPath().c_str(), "rb");
        uint8_t buf[0x20] = {0};
        size_t n = f ? fread(buf, 1, sizeof buf, f) : 0;
        if (f) fclose(f);
        if (n < 0x14 || buf[0x10] != 'N' || buf[0x11] != 'R' || buf[0x12] != 'O' || buf[0x13] != '0' || ctx.got < 0x1000) { fail("ダウンロードしたファイルが nro ではありません"); return; }
    }
    if (!rel.sha256.empty()) {
        u8 h[SHA256_HASH_SIZE];
        sha256ContextGetHash(&ctx.sha, h);
        char hex[SHA256_HASH_SIZE * 2 + 1];
        for (int i = 0; i < SHA256_HASH_SIZE; i++) snprintf(hex + i * 2, 3, "%02x", h[i]);
        if (rel.sha256 != hex) { fail("ファイルの検証(SHA-256)に失敗しました"); return; }
    }
    remove(newPath().c_str());
    if (rename(partPath().c_str(), newPath().c_str()) != 0) { fail("更新ファイルを保存できません"); return; }
    downloadedOk = true;
    dl = DONE;
}

// ---------- メインスレッドから使う API ----------
static void reap() {  // 終わったスレッドを片付ける
    if (thActive && chk != RUNNING && dl != RUNNING) { threadWaitForExit(&th); threadClose(&th); thActive = false; }
}
static bool launch(ThreadFunc fn) {
    reap();
    if (thActive) return false;
    if (R_FAILED(threadCreate(&th, fn, nullptr, nullptr, 0x100000, 0x2C, -2))) return false;
    if (R_FAILED(threadStart(&th))) { threadClose(&th); return false; }
    thActive = true;
    return true;
}
inline bool busy() { return chk == RUNNING || dl == RUNNING; }
inline void startCheck() {
    if (busy()) return;
    reap();
    chk = RUNNING; chkErr.clear(); isNewerRel = false; cancelFlag = false;
    if (!launch(checkMain)) { chkErr = "通信スレッドを開始できません"; chk = FAILED; }
}
inline void startDownload() {
    if (busy()) return;
    reap();
    dl = RUNNING; dlErr.clear(); dlNow = 0; dlTotal = rel.size; cancelFlag = false;
    if (!launch(downloadMain)) { dlErr = "通信スレッドを開始できません"; dl = FAILED; }
}
inline void cancel() { cancelFlag = true; }
inline bool available() { return chk == DONE && isNewerRel && rel.hasAsset; }
inline bool ready() { return downloadedOk && dl == DONE; }

inline void shutdown() {
    cancelFlag = true;
    if (thActive) { threadWaitForExit(&th); threadClose(&th); thActive = false; }
    if (curlUp) { curl_global_cleanup(); curlUp = false; }
    if (netUp) { socketExit(); netUp = false; }
    if (nifmUp) { nifmExit(); nifmUp = false; }
}

// ---------- 適用 (romfsExit の後で呼ぶ) ----------
inline void writeLog(const std::string& s) {
    FILE* f = fopen(logPath().c_str(), "w");
    if (f) { fputs(s.c_str(), f); fclose(f); }
}
// 戻り値: 差し替えに成功したら true
inline bool apply() {
    if (!ready()) return false;
    std::string bak = nroPath + ".bak";
    struct stat st;
    remove(bak.c_str());
    bool hadOld = stat(nroPath.c_str(), &st) == 0;
    if (hadOld && rename(nroPath.c_str(), bak.c_str()) != 0) { writeLog("ng\n古い nro を退避できませんでした"); return false; }
    if (rename(newPath().c_str(), nroPath.c_str()) != 0) {
        if (hadOld) rename(bak.c_str(), nroPath.c_str());  // 元に戻す
        writeLog("ng\n新しい nro を配置できませんでした");
        return false;
    }
    remove(bak.c_str());
    writeLog("ok\n" + rel.tag);
    return true;
}
// 起動時: 前回の更新結果を取り出す (無ければ空)。ok なら true
inline std::string takeLog(bool& ok) {
    ok = false;
    std::vector<uint8_t> d;
    if (!util::readFile(logPath(), d)) return "";
    remove(logPath().c_str());
    std::string s((const char*)d.data(), d.size());
    size_t nl = s.find('\n');
    std::string head = s.substr(0, nl), rest = nl == std::string::npos ? "" : s.substr(nl + 1);
    ok = head == "ok";
    return rest;
}
inline void relaunch() {
    if (envHasNextLoad()) envSetNextLoad(nroPath.c_str(), ("\"" + nroPath + "\"").c_str());
}

}  // namespace upd

// NX PCK Studio — PCK-studio-Web と同じ画面構成 (メニュー / アセット一覧 / プレビュー / アセット詳細)
// .pck を作って sd:/atmosphere/contents/01006BD001E06000/romfs/NX/DLC/<パック名>/<ファイル名>.pck に置く
#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <dirent.h>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>
#include "pck.hpp"
#include "skin3d.hpp"
#include "util.hpp"

static const char* APP_VER = "v1.1.0";

// ---------- 色 (PCK-studio-Web の CSS と同じ) ----------
struct C { uint8_t r, g, b, a = 255; };
static const C BG{0x2b, 0x2b, 0x2b}, BAR{0x3a, 0x3a, 0x3a}, LINE{0x55, 0x55, 0x55}, SIDE{0x33, 0x33, 0x33},
    TXT{0xe8, 0xe8, 0xe8}, MUTED{0xaa, 0xaa, 0xaa}, GREEN{0x4a, 0x8f, 0x29}, GREEN_HI{0x5a, 0xa5, 0x34},
    SELBG{0x2f, 0x5a, 0x1e}, YEL{0xff, 0xff, 0x55}, CYAN{0xaa, 0xff, 0xff}, ORANGE{0xff, 0xb8, 0x70},
    RED{0xd3, 0x2f, 0x2f}, RED_HI{0xff, 0x52, 0x52}, WHITE{255, 255, 255}, ROWLINE{0x3a, 0x3a, 0x3a},
    HOVER{0x55, 0x55, 0x55}, LOCKEY{0x80, 0xff, 0x80}, BLUE{0x2f, 0x6f, 0xbf}, WARNBG{0x5a, 0x1f, 0x1f};

static SDL_Renderer* R;
static bool running = true;

// ---------- フォント: MCFont → Pixelify Sans → DotGothic16 → 本体の共有フォント (HTML の font-family と同じ順) ----------
enum { SZ_S = 0, SZ_M = 1, SZ_L = 2 };
static const int PXSIZE[3] = {16, 24, 32};
static std::vector<uint8_t> fdata[3];
static PlFontData sysFont;
static TTF_Font* FNT[3][4];

static void loadFonts() {
    const char* files[3] = {"romfs:/minecraft.ttf", "romfs:/PixelifySans.ttf", "romfs:/DotGothic16.ttf"};
    for (int i = 0; i < 3; i++) util::readFile(files[i], fdata[i]);  // 読んだらすぐ閉じる (終了時に開きっぱなしのファイルを残さない)
    plGetSharedFontByType(&sysFont, PlSharedFontType_Standard);
    for (int s = 0; s < 3; s++)
        for (int k = 0; k < 4; k++) {
            const void* p = nullptr; size_t n = 0;
            if (k < 3) { if (fdata[k].empty()) { FNT[s][k] = nullptr; continue; } p = fdata[k].data(); n = fdata[k].size(); }
            else { p = sysFont.address; n = sysFont.size; }
            FNT[s][k] = TTF_OpenFontRW(SDL_RWFromConstMem(p, (int)n), 1, PXSIZE[s]);
        }
}
static void closeFonts() {
    for (int s = 0; s < 3; s++) for (int k = 0; k < 4; k++) if (FNT[s][k]) { TTF_CloseFont(FNT[s][k]); FNT[s][k] = nullptr; }
}

static bool hasGlyph(TTF_Font* f, uint32_t cp) {
    if (!f) return false;
#ifdef SDL_TTF_VERSION_ATLEAST
#if SDL_TTF_VERSION_ATLEAST(2, 0, 18)
    return TTF_GlyphIsProvided32(f, cp) != 0;
#else
    return cp < 0x10000 && TTF_GlyphIsProvided(f, (Uint16)cp) != 0;
#endif
#else
    return cp < 0x10000 && TTF_GlyphIsProvided(f, (Uint16)cp) != 0;
#endif
}
static uint32_t nextCp(const std::string& s, size_t& i) {
    uint32_t c = (uint8_t)s[i++];
    int n = 0;
    if (c < 0x80) return c;
    else if ((c >> 5) == 6) { c &= 0x1F; n = 1; }
    else if ((c >> 4) == 14) { c &= 0x0F; n = 2; }
    else if ((c >> 3) == 30) { c &= 0x07; n = 3; }
    else return '?';
    for (int k = 0; k < n && i < s.size(); k++, i++) c = (c << 6) | ((uint8_t)s[i] & 0x3F);
    return c;
}
struct Run { int fi; std::string t; };
static std::vector<Run> splitRuns(int sz, const std::string& s) {
    std::vector<Run> runs;
    int cur = -1;
    for (size_t i = 0; i < s.size();) {
        size_t st = i;
        uint32_t cp = nextCp(s, i);
        int fi = -1;
        if (cur >= 0 && cp == ' ' && hasGlyph(FNT[sz][cur], cp)) fi = cur;
        else for (int k = 0; k < 4; k++) if (hasGlyph(FNT[sz][k], cp)) { fi = k; break; }
        if (fi < 0) { fi = FNT[sz][3] ? 3 : (cur >= 0 ? cur : 0); while (fi < 4 && !FNT[sz][fi]) fi++; if (fi >= 4) fi = 0; }
        if (fi != cur) { runs.push_back({fi, ""}); cur = fi; }
        runs.back().t += s.substr(st, i - st);
    }
    return runs;
}
static int textW(int sz, const std::string& s) {
    int w = 0;
    for (auto& r : splitRuns(sz, s)) { int rw = 0, rh = 0; if (FNT[sz][r.fi] && TTF_SizeUTF8(FNT[sz][r.fi], r.t.c_str(), &rw, &rh) == 0) w += rw; }
    return w;
}
static SDL_Surface* renderMixed(int sz, const std::string& s, C c) {
    std::vector<Run> runs = splitRuns(sz, s);
    std::vector<SDL_Surface*> sf;
    std::vector<int> asc;
    int w = 0, maxAsc = 0;
    for (auto& r : runs) {
        TTF_Font* f = FNT[sz][r.fi];
        SDL_Surface* x = f ? TTF_RenderUTF8_Blended(f, r.t.c_str(), SDL_Color{c.r, c.g, c.b, 255}) : nullptr;
        sf.push_back(x);
        asc.push_back(f ? TTF_FontAscent(f) : 0);
        if (x) { w += x->w; maxAsc = std::max(maxAsc, asc.back()); }
    }
    int h = 0;
    for (size_t i = 0; i < sf.size(); i++) if (sf[i]) h = std::max(h, maxAsc - asc[i] + sf[i]->h);
    if (!w || !h) { for (auto x : sf) if (x) SDL_FreeSurface(x); return nullptr; }
    SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_FillRect(dst, nullptr, 0);
    int x = 0;
    for (size_t i = 0; i < sf.size(); i++) {
        if (!sf[i]) continue;
        SDL_SetSurfaceBlendMode(sf[i], SDL_BLENDMODE_NONE);
        SDL_Rect d{x, maxAsc - asc[i], sf[i]->w, sf[i]->h};
        SDL_BlitSurface(sf[i], nullptr, dst, &d);
        x += sf[i]->w;
        SDL_FreeSurface(sf[i]);
    }
    return dst;
}

// ---------- 描画ヘルパ ----------
static void fill(int x, int y, int w, int h, C c) {
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_Rect r{x, y, w, h};
    SDL_RenderFillRect(R, &r);
}
static void outline(int x, int y, int w, int h, C c, int t = 2) {
    fill(x, y, w, t, c); fill(x, y + h - t, w, t, c); fill(x, y, t, h, c); fill(x + w - t, y, t, h, c);
}
struct TC { SDL_Texture* t; int w, h; };
static std::map<std::string, TC> tcache;
// y は文字の縦中心。anchor: 0=左揃え 1=中央 2=右揃え
static void text(int sz, const std::string& s, int x, int cy, C c, int anchor = 0) {
    if (s.empty()) return;
    std::string key = std::to_string(sz) + "|" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + "|" + s;
    auto it = tcache.find(key);
    if (it == tcache.end()) {
        if (tcache.size() > 700) { for (auto& kv : tcache) SDL_DestroyTexture(kv.second.t); tcache.clear(); }
        SDL_Surface* sf = renderMixed(sz, s, c);
        if (!sf) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(R, sf);
        TC tc{t, sf->w, sf->h};
        SDL_FreeSurface(sf);
        it = tcache.emplace(key, tc).first;
    }
    SDL_Rect d{x, cy - it->second.h / 2, it->second.w, it->second.h};
    if (anchor == 1) d.x -= d.w / 2; else if (anchor == 2) d.x -= d.w;
    SDL_RenderCopy(R, it->second.t, nullptr, &d);
}
static std::string clipText(int sz, const std::string& s, int maxW) {  // 幅に収まるまで末尾を削って ... を付ける
    if (textW(sz, s) <= maxW) return s;
    std::vector<size_t> cuts;
    for (size_t i = 0; i < s.size();) { cuts.push_back(i); nextCp(s, i); }
    size_t lo = 0, hi = cuts.size();
    while (lo + 1 < hi) { size_t mid = (lo + hi) / 2; if (textW(sz, s.substr(0, cuts[mid]) + "...") <= maxW) lo = mid; else hi = mid; }
    return s.substr(0, cuts[lo]) + "...";
}
static std::vector<std::string> wrapText(int sz, const std::string& s, int maxW) {
    std::vector<std::string> out;
    std::string line;
    for (size_t i = 0; i < s.size();) {
        size_t st = i;
        uint32_t cp = nextCp(s, i);
        if (cp == '\n') { out.push_back(line); line.clear(); continue; }
        std::string add = s.substr(st, i - st);
        if (!line.empty() && textW(sz, line + add) > maxW) { out.push_back(line); line.clear(); }
        line += add;
    }
    if (!line.empty() || out.empty()) out.push_back(line);
    return out;
}

// ---------- タッチ / ヒット領域 ----------
struct Hit { SDL_Rect r; std::function<void()> fn; };
static std::vector<Hit> hits;
static void hit(int x, int y, int w, int h, std::function<void()> fn) { hits.push_back({{x, y, w, h}, std::move(fn)}); }
static void hitIn(SDL_Rect area, int x, int y, int w, int h, std::function<void()> fn) {  // スクロール領域でクリップ
    SDL_Rect r{x, y, w, h}, o;
    if (SDL_IntersectRect(&r, &area, &o)) hits.push_back({o, std::move(fn)});
}
static void dispatchTap(int x, int y) {
    for (size_t i = hits.size(); i-- > 0;) {
        const SDL_Rect& r = hits[i].r;
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) { auto fn = hits[i].fn; if (fn) fn(); return; }
    }
}

struct Input { uint64_t down = 0; bool tap = false; int tx = 0, ty = 0; int dragDy = 0, dragDx = 0, sx = 0, sy = 0, stx = 0, sty = 0; };
static PadState pad;
static void pollInput(Input& in) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {}
    padUpdate(&pad);
    in = Input();
    in.down = padGetButtonsDown(&pad);
    static bool touching = false, dragged = false;
    static int sx = 0, sy = 0, lx = 0, ly = 0;
    HidTouchScreenState ts = {0};
    hidGetTouchScreenStates(&ts, 1);
    if (ts.count > 0) {
        int x = (int)ts.touches[0].x, y = (int)ts.touches[0].y;
        if (!touching) { touching = true; dragged = false; sx = lx = x; sy = ly = y; }
        else {
            if (!dragged && (abs(x - sx) > 14 || abs(y - sy) > 14)) dragged = true;
            if (dragged) { in.dragDy = y - ly; in.dragDx = x - lx; }
            lx = x; ly = y;
        }
    } else if (touching) {
        touching = false;
        if (!dragged) { in.tap = true; in.tx = lx; in.ty = ly; }
    }
    in.sx = sx; in.sy = sy;
    HidAnalogStickState rs = padGetStickPos(&pad, 1);
    in.stx = rs.x; in.sty = rs.y;
}

// ---------- 画像 ----------
struct Img { std::string path, name; uint32_t w = 0, h = 0; size_t size = 0; };
static std::map<std::string, SDL_Texture*> imgCache;
static SDL_Texture* imgTex(const std::string& path) {
    auto it = imgCache.find(path);
    if (it != imgCache.end()) return it->second;
    SDL_Texture* t = IMG_LoadTexture(R, path.c_str());
    imgCache[path] = t;
    return t;
}
static void clearImgCache() { for (auto& kv : imgCache) if (kv.second) SDL_DestroyTexture(kv.second); imgCache.clear(); }
static std::map<uint64_t, SDL_Texture*> atex;  // PCK内のPNG用
static SDL_Texture* assetTex(const std::vector<uint8_t>& d) {
    uint64_t h = 1469598103934665603ULL;
    for (uint8_t b : d) h = (h ^ b) * 1099511628211ULL;
    h ^= d.size();
    auto it = atex.find(h);
    if (it != atex.end()) return it->second;
    if (atex.size() > 48) { for (auto& kv : atex) if (kv.second) SDL_DestroyTexture(kv.second); atex.clear(); }
    SDL_Texture* t = IMG_LoadTexture_RW(R, SDL_RWFromConstMem(d.data(), (int)d.size()), 1);
    atex[h] = t;
    return t;
}
static void drawTex(SDL_Texture* t, uint32_t w, uint32_t h, int x, int y, int bw, int bh) {  // 整数倍でドット絵のまま拡大
    if (!t || !w || !h) return;
    int s = std::max(1, (int)std::min(bw / w, bh / h));
    SDL_Rect d{x + (bw - (int)w * s) / 2, y + (bh - (int)h * s) / 2, (int)w * s, (int)h * s};
    for (int yy = 0; yy < d.h; yy += 16)  // 透過が分かる市松模様
        for (int xx = 0; xx < d.w; xx += 16) fill(d.x + xx, d.y + yy, std::min(16, d.w - xx), std::min(16, d.h - yy), ((xx + yy) / 16) % 2 ? C{0x3a, 0x3a, 0x3a} : C{0x30, 0x30, 0x30});
    SDL_RenderCopy(R, t, nullptr, &d);
}
static std::string imgDir() { return std::string(util::BASE) + "images/"; }
static std::vector<Img> scanImages() {
    std::vector<Img> v;
    if (DIR* d = opendir(imgDir().c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (!util::isPng(n)) continue;
            Img im; im.name = n; im.path = imgDir() + n;
            std::vector<uint8_t> data;
            if (!util::readFile(im.path, data) || !pck::pngSize(data, im.w, im.h)) continue;
            im.size = data.size();
            v.push_back(im);
        }
        closedir(d);
    }
    std::sort(v.begin(), v.end(), [](const Img& a, const Img& b) { return a.name < b.name; });
    return v;
}

// ---------- パッケージ (Webツールの「新規パッケージ」と同じ構成) ----------
static util::Config cfg;
static std::string status = "";
static C statusColor = YEL;
static void setStatus(const std::string& s, bool bad = false) { status = s; statusColor = bad ? RED_HI : YEL; }

struct Pkg { std::vector<pck::Asset> a; std::string name; bool dirty = false; } P;
static std::vector<std::pair<std::string, std::string>> locEntries;
static int sel = 0;
static std::string lastFolder, lastFile;

static uint32_t rnd() {
    static uint64_t s = armGetSystemTick();
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(s >> 33);
}
static pck::Prop* findProp(pck::Asset& a, const std::string& k) { for (auto& p : a.props) if (p.key == k) return &p; return nullptr; }
static std::string getProp(const pck::Asset& a, const std::string& k) { for (auto& p : a.props) if (p.key == k) return p.value; return ""; }
static void setProp(pck::Asset& a, const std::string& k, const std::string& v) { if (auto p = findProp(a, k)) p->value = v; else a.props.push_back({k, v}); }
static void delProp(pck::Asset& a, const std::string& k) { for (size_t i = 0; i < a.props.size(); i++) if (a.props[i].key == k) { a.props.erase(a.props.begin() + i); return; } }
static bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
static bool isSkin(const pck::Asset& a) { return a.type == 0 && startsWith(a.name, "dlcskin"); }
static bool isCape(const pck::Asset& a) { return a.type == 1 && startsWith(a.name, "dlccape"); }
static bool isPng(const pck::Asset& a) { return a.type == 0 || a.type == 1; }
static int findAsset(const std::string& name) { for (size_t i = 0; i < P.a.size(); i++) if (P.a[i].name == name) return (int)i; return -1; }
static std::string packNameEff() {
    if (!P.name.empty()) return P.name;
    for (auto& a : P.a) if (isSkin(a) && !getProp(a, "DISPLAYNAME").empty()) return getProp(a, "DISPLAYNAME");
    return "Skin Pack";
}
static void rebuildLoc() {  // 〜ID プロパティ(IDS_…)と対応する文字列から localisation.loc を作る
    locEntries.clear();
    auto add = [&](const std::string& k, const std::string& v) { for (auto& e : locEntries) if (e.first == k) return; locEntries.push_back({k, v}); };
    add("IDS_DISPLAY_NAME", packNameEff());
    for (auto& a : P.a)
        for (auto& p : a.props)
            if (p.key.size() > 2 && p.key.compare(p.key.size() - 2, 2, "ID") == 0 && startsWith(p.value, "IDS_")) {
                std::string t = getProp(a, p.key.substr(0, p.key.size() - 2));
                if (!t.empty()) add(p.value, t);
            }
    if (P.a.size() > 1) P.a[1].data = pck::buildLoc(locEntries);
}
static void markDirty() { P.dirty = true; rebuildLoc(); }
static void newPkg() {
    P.a.clear(); P.name.clear(); P.dirty = false; sel = 0;
    pck::Asset root; root.name = "0"; root.type = 4; root.props.push_back({"PACKID", std::to_string(1 + rnd() % 0xFFFFFE)});
    pck::Asset loc; loc.name = "localisation.loc"; loc.type = 6;
    P.a.push_back(root); P.a.push_back(loc);
    rebuildLoc();
}
static uint32_t newUniqueId() {
    for (;;) {
        uint32_t id = 100000 + rnd() % 99899998;
        if (findAsset("dlcskin" + pck::pad8(id) + ".png") < 0) return id;
    }
}
static std::string skinIds(const pck::Asset& a) { return a.name.substr(7, a.name.size() >= 11 ? a.name.size() - 11 : 0); }  // dlcskin<ID>.png
static void applyId(pck::Asset& s, const std::string& ids) {
    s.name = "dlcskin" + ids + ".png";
    if (findProp(s, "DISPLAYNAMEID")) setProp(s, "DISPLAYNAMEID", "IDS_dlcskin" + ids + "_DISPLAYNAME");
    if (findProp(s, "THEMENAMEID")) setProp(s, "THEMENAMEID", "IDS_dlcskin" + ids + "_THEMENAME");
}
static bool readImg(const Img& im, std::vector<uint8_t>& d) {
    if (!util::readFile(im.path, d)) { setStatus("画像を読めません: " + im.name, true); return false; }
    return true;
}
static void addSkin(const Img& im, const std::string& name) {
    std::vector<uint8_t> d;
    if (!readImg(im, d)) return;
    std::string ids = pck::pad8(newUniqueId()), nameKey = "IDS_dlcskin" + ids + "_DISPLAYNAME";
    pck::Asset s; s.name = "dlcskin" + ids + ".png"; s.type = 0; s.data = d;
    s.props = {{"DISPLAYNAME", name}, {"DISPLAYNAMEID", nameKey}, {"ANIM", "0x00040000"}, {"GAME_FLAGS", "0x18"}, {"FREE", "1"}};
    P.a.push_back(s);
    if (P.name.empty()) P.name = name;
    sel = (int)P.a.size() - 1;
    markDirty();
    setStatus("スキンを追加しました: " + name);
}
static void setTheme(pck::Asset& s, const std::string& theme) {
    if (theme.empty()) { delProp(s, "THEMENAME"); delProp(s, "THEMENAMEID"); return; }
    std::string ids = skinIds(s);
    if (!findProp(s, "THEMENAME")) {
        size_t pos = 0;
        for (size_t i = 0; i < s.props.size(); i++) if (s.props[i].key == "DISPLAYNAMEID") pos = i + 1;
        s.props.insert(s.props.begin() + pos, pck::Prop{"THEMENAMEID", "IDS_dlcskin" + ids + "_THEMENAME"});
        s.props.insert(s.props.begin() + pos, pck::Prop{"THEMENAME", theme});
    } else setProp(s, "THEMENAME", theme);
}
static bool setSkinId(int ai, uint32_t id) {
    if (id < 100000 || id > 99999998) { setStatus("スキンIDは 100000〜99999998 にしてください", true); return false; }
    std::string ids = pck::pad8(id);
    int other = findAsset("dlcskin" + ids + ".png");
    if (other >= 0 && other != ai) { setStatus("そのIDは既に使われています", true); return false; }
    pck::Asset& s = P.a[ai];
    std::string capeOld = getProp(s, "CAPEPATH");
    applyId(s, ids);
    if (!capeOld.empty()) {
        int ci = findAsset(capeOld);
        if (ci >= 0) { P.a[ci].name = "dlccape" + ids + ".png"; setProp(s, "CAPEPATH", P.a[ci].name); }
    }
    markDirty();
    return true;
}
static void replaceData(int ai, const Img& im) {
    std::vector<uint8_t> d;
    if (!readImg(im, d)) return;
    P.a[ai].data = d;
    markDirty();
    setStatus("画像を置き換えました: " + im.name);
}
static int setCape(int ai, const Img& im) {  // スキンの位置(マントを前に挿入すると +1)を返す
    std::vector<uint8_t> d;
    if (!readImg(im, d)) return ai;
    std::string capeName = "dlccape" + skinIds(P.a[ai]) + ".png";
    int ci = findAsset(capeName);
    if (ci >= 0) P.a[ci].data = d;
    else {
        pck::Asset c; c.name = capeName; c.type = 1; c.data = d;
        P.a.insert(P.a.begin() + ai, c);
        ai++;
        if (sel >= ai - 1) sel = ai;
    }
    setProp(P.a[ai], "CAPEPATH", capeName);
    markDirty();
    setStatus("マントを設定しました: " + im.name);
    return ai;
}
static void eraseAsset(int i) {
    P.a.erase(P.a.begin() + i);
    if (i < sel) sel--;
    if (sel >= (int)P.a.size()) sel = (int)P.a.size() - 1;
}
static void removeCape(int ai) {
    int ci = findAsset(getProp(P.a[ai], "CAPEPATH"));
    delProp(P.a[ai], "CAPEPATH");
    if (ci >= 0) eraseAsset(ci);
    markDirty();
    setStatus("マントを外しました");
}
static void deleteAsset(int i) {
    if (i < 2 || i >= (int)P.a.size()) return;
    if (isSkin(P.a[i])) {
        int ci = findAsset(getProp(P.a[i], "CAPEPATH"));
        eraseAsset(i);
        if (ci > i) ci--;
        if (ci >= 0 && ci < (int)P.a.size() && isCape(P.a[ci])) eraseAsset(ci);
    } else {
        std::string n = P.a[i].name;
        for (auto& a : P.a) if (getProp(a, "CAPEPATH") == n) delProp(a, "CAPEPATH");
        eraseAsset(i);
    }
    markDirty();
    setStatus("アセットを削除しました");
}
static void duplicateSkin(int ai) {
    pck::Asset s = P.a[ai];
    std::string ids = pck::pad8(newUniqueId());
    std::string capeOld = getProp(s, "CAPEPATH");
    applyId(s, ids);
    if (!capeOld.empty()) {
        int ci = findAsset(capeOld);
        if (ci >= 0) { pck::Asset c = P.a[ci]; c.name = "dlccape" + ids + ".png"; setProp(s, "CAPEPATH", c.name); P.a.push_back(c); }
        else delProp(s, "CAPEPATH");
    }
    P.a.push_back(s);
    sel = (int)P.a.size() - 1;
    markDirty();
    setStatus("スキンを複製しました (IDを振り直しました)");
}

// ---------- UI 状態 ----------
enum Pane { LIST, DETAIL, MENU };
static Pane pane = LIST, prevPane = LIST;
static int menuFocus = 0, detFocus = 0, listScroll = 0, detScroll = 0;

// レイアウト (1280x720)。HTML の「メニューバー / 情報バー / サイドバー / プレビュー / 詳細」と同じ並び
static const int MENU_H = 40, INFO_Y = 40, INFO_H = 32, MAIN_Y = 72, FOOT_Y = 692, FOOT_H = 28;
static const int SB_W = 380, HDR_H = 36, LIST_ROW = 56, ACT_H = 56, CT_X = 380, CT_W = 900, DET_Y = 372, DET_ROW = 40;
static const SDL_Rect LIST_RECT{0, MAIN_Y + HDR_H, SB_W, FOOT_Y - MAIN_Y - HDR_H - ACT_H};
static const SDL_Rect PV3D{CT_X + 10, MAIN_Y + HDR_H + 4, 460, DET_Y - MAIN_Y - HDR_H - 9};  // 3Dプレビュー
static float yaw3d = 0.6f, pitch3d = 0.2f, zoom3d = 1.0f;
static bool autoRot = true;
static const SDL_Rect DET_RECT{CT_X, DET_Y + HDR_H, CT_W, FOOT_Y - DET_Y - HDR_H};

static bool keyboard(const char* header, const std::string& init, std::string& out, int maxLen, bool numeric = false) {
    SwkbdConfig k;
    if (R_FAILED(swkbdCreate(&k, 0))) return false;
    swkbdConfigMakePresetDefault(&k);
    swkbdConfigSetHeaderText(&k, header);
    swkbdConfigSetInitialText(&k, init.c_str());
    swkbdConfigSetStringLenMax(&k, maxLen);
    if (numeric) swkbdConfigSetType(&k, SwkbdType_NumPad);
    char buf[512] = {0};
    Result rc = swkbdShow(&k, buf, sizeof buf);
    swkbdClose(&k);
    if (R_FAILED(rc)) return false;
    out = buf;
    return true;
}

static void drawAll();

// ---------- ダイアログ (HTML の .modal-dialog) ----------
static void overlay() { fill(0, 0, 1280, 720, C{0, 0, 0, 180}); }
// buttons の番号を返す。B / キャンセルで -1
static int dialog(const std::string& title, const std::string& msg, std::vector<std::string> buttons, int def) {
    std::vector<std::string> lines = wrapText(SZ_M, msg, 700);
    int bw = 200, n = (int)buttons.size();
    int w = 760, h = 40 + 24 + (int)lines.size() * 34 + 24 + 52 + 24, x = (1280 - w) / 2, y = (720 - h) / 2;
    int f = def, result = -2;
    while (result == -2) {
        if (!appletMainLoop()) { running = false; return -1; }
        Input in; pollInput(in);
        drawAll();
        hits.clear();
        overlay();
        fill(x - 2, y - 2, w + 4, h + 4, LINE); fill(x, y, w, h, SIDE);
        fill(x, y, w, 40, BAR); fill(x, y + 40, w, 1, LINE);
        text(SZ_M, title, x + 20, y + 20, WHITE);
        for (size_t i = 0; i < lines.size(); i++) text(SZ_M, lines[i], x + 30, y + 40 + 24 + (int)i * 34 + 17, TXT);
        int by = y + h - 24 - 52, bx0 = x + (w - (n * bw + (n - 1) * 20)) / 2;
        for (int i = 0; i < n; i++) {
            int bx = bx0 + i * (bw + 20);
            fill(bx, by, bw, 52, i == 0 ? (f == i ? GREEN_HI : GREEN) : (f == i ? HOVER : LINE));
            if (f == i) outline(bx - 3, by - 3, bw + 6, 58, YEL);
            text(SZ_M, buttons[i], bx + bw / 2, by + 26, WHITE, 1);
            hit(bx, by, bw, 52, [&result, i] { result = i; });
        }
        if (in.tap) dispatchTap(in.tx, in.ty);
        if (in.down & HidNpadButton_Left) f = (f + n - 1) % n;
        if (in.down & HidNpadButton_Right) f = (f + 1) % n;
        if (in.down & HidNpadButton_A) result = f;
        if (in.down & HidNpadButton_B) result = -1;
        SDL_RenderPresent(R);
    }
    return result;
}

// 画像選択ダイアログ
static bool pickImage(const char* title, std::function<bool(const Img&)> ok, Img& out) {
    clearImgCache();
    std::vector<Img> list;
    for (auto& im : scanImages()) if (ok(im)) list.push_back(im);
    int f = 0, scroll = 0, result = -2;
    const int X = 90, Y = 60, W = 1100, H = 600, RH = 44, LX = X + 20, LY = Y + 60, LW = 600, LH = 396;
    SDL_Rect area{LX, LY, LW, LH};
    while (result == -2) {
        if (!appletMainLoop()) { running = false; return false; }
        Input in; pollInput(in);
        drawAll();
        hits.clear();
        overlay();
        fill(X - 2, Y - 2, W + 4, H + 4, LINE); fill(X, Y, W, H, SIDE);
        fill(X, Y, W, 40, BAR); fill(X, Y + 40, W, 1, LINE);
        text(SZ_M, title, X + 20, Y + 20, WHITE);
        int n = (int)list.size();
        if (!n) {
            text(SZ_M, "該当する画像がありません", LX, LY + 20, TXT);
            text(SZ_S, "sd:/switch/nxpckstudio/images/ に PNG を入れてください", LX, LY + 60, MUTED);
        }
        if (in.dragDy && in.sx >= LX && in.sx < LX + LW && in.sy >= LY && in.sy < LY + LH) scroll -= in.dragDy;
        scroll = std::max(0, std::min(scroll, std::max(0, n * RH - LH)));
        SDL_RenderSetClipRect(R, &area);
        for (int i = 0; i < n; i++) {
            int ry = LY + i * RH - scroll;
            if (ry + RH < LY || ry > LY + LH) continue;
            fill(LX, ry, LW, RH - 4, i == f ? SELBG : BG);
            if (i == f) fill(LX, ry, 4, RH - 4, GREEN);
            text(SZ_M, clipText(SZ_M, list[i].name, 440), LX + 14, ry + RH / 2 - 2, i == f ? YEL : TXT);
            text(SZ_S, std::to_string(list[i].w) + "x" + std::to_string(list[i].h), LX + LW - 10, ry + RH / 2 - 2, MUTED, 2);
            hitIn(area, LX, ry, LW, RH - 4, [&f, &result, i] { if (f == i) result = i; else f = i; });
        }
        SDL_RenderSetClipRect(R, nullptr);
        fill(X + 660, LY, 420, LH, BG); outline(X + 660, LY, 420, LH, LINE, 1);
        if (n) {
            const Img& im = list[f];
            drawTex(imgTex(im.path), im.w, im.h, X + 670, LY + 10, 400, LH - 60);
            text(SZ_S, std::to_string(im.w) + "x" + std::to_string(im.h) + "  " + std::to_string(im.size) + " bytes" + (im.size > 0x7FFF ? "  (32767超)" : ""), X + 870, LY + LH - 24, im.size > 0x7FFF ? RED_HI : MUTED, 1);
        }
        int by = Y + H - 24 - 48;
        fill(X + W - 20 - 200, by, 200, 48, LINE); text(SZ_M, "キャンセル", X + W - 120, by + 24, WHITE, 1);
        hit(X + W - 20 - 200, by, 200, 48, [&result] { result = -1; });
        fill(X + W - 40 - 400, by, 200, 48, n ? GREEN : LINE); text(SZ_M, "決定", X + W - 40 - 300, by + 24, WHITE, 1);
        hit(X + W - 40 - 400, by, 200, 48, [&result, &f, n] { if (n) result = f; });
        text(SZ_S, "↑↓:選択  A:決定  B:キャンセル", X + 20, by + 24, MUTED);
        if (in.tap) dispatchTap(in.tx, in.ty);
        if (n && (in.down & HidNpadButton_Up)) f = (f + n - 1) % n;
        if (n && (in.down & HidNpadButton_Down)) f = (f + 1) % n;
        if (n) { int pos = f * RH; if (pos < scroll) scroll = pos; else if (pos + RH > scroll + LH) scroll = pos + RH - LH; }
        if (n && (in.down & HidNpadButton_A)) result = f;
        if (in.down & HidNpadButton_B) result = -1;
        SDL_RenderPresent(R);
    }
    if (result >= 0 && result < (int)list.size()) { out = list[result]; return true; }
    return false;
}
static bool fitsSkin(const Img& im) { return im.w == 64 && (im.h == 64 || im.h == 32); }
static bool fitsCape(const Img& im) { return im.w == 64 && im.h == 32; }
static bool anyPng(const Img&) { return true; }

// ---------- 操作 (メニュー / 詳細から呼ぶ) ----------
static void actAddSkin() {
    Img im;
    if (!pickImage("スキン画像を選ぶ (64x64 / 64x32)", fitsSkin, im)) return;
    std::string base = im.name.substr(0, im.name.size() - 4), name;
    if (!keyboard("スキン名", base, name, 30) || name.empty()) return;
    addSkin(im, name);
}
static void actSave() {
    bool anySkin = false, big = false;
    for (auto& a : P.a) { if (isSkin(a)) anySkin = true; if (isPng(a) && a.data.size() > 0x7FFF) big = true; }
    if (!anySkin) { dialog("保存できません", "スキンがありません。\n「スキン追加」から画像を選んでください。", {"OK"}, 0); return; }
    if (big && dialog("警告", "32767バイトを超える画像があります。ゲームが落ちる恐れがあります。\nこのまま保存しますか?", {"保存する", "やめる"}, 1) != 0) return;
    std::string folder, file;
    std::string hint = "sd:/atmosphere/contents/" + std::string(util::TITLE_ID) + "/romfs/NX/DLC/";
    if (!keyboard("① スキンパックのフォルダー名 (NX/DLC/この名前/)", lastFolder.empty() ? util::cleanName(packNameEff()) : lastFolder, folder, 40)) return;
    folder = util::cleanName(folder);
    if (folder.empty()) { setStatus("フォルダー名が空です", true); return; }
    if (!keyboard("② PCKファイル名 (.pck は自動で付きます)", lastFile.empty() ? folder : lastFile, file, 40)) return;
    file = util::cleanName(file);
    if (file.size() > 4) { std::string t = file.substr(file.size() - 4); for (auto& c : t) c = (char)tolower((unsigned char)c); if (t == ".pck") file = file.substr(0, file.size() - 4); }
    if (file.empty()) { setStatus("ファイル名が空です", true); return; }
    std::string dir = util::dlcRoot() + folder + "/", path = dir + file + ".pck";
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && dialog("上書き確認", "このファイルは既にあります。上書きしますか?\n" + folder + "/" + file + ".pck", {"上書き", "やめる"}, 1) != 0) return;
    rebuildLoc();
    std::vector<uint8_t> out = pck::build(P.a, 8, cfg.little);
    util::mkdirs(dir);
    if (!util::writeAtomic(path, out)) { setStatus("書き込みに失敗しました (SDの空き容量を確認)", true); dialog("保存失敗", "書き込みに失敗しました。\nSDカードの空き容量を確認してください。", {"OK"}, 0); return; }
    lastFolder = folder; lastFile = file; P.dirty = false;
    setStatus("配置しました: " + folder + "/" + file + ".pck");
    dialog("配置しました", hint + folder + "/" + file + ".pck\n\nゲームを再起動してください。", {"OK"}, 0);
}
static void actNew() {
    if (P.dirty && dialog("新規", "保存していない内容は破棄されます。続けますか?", {"新規作成", "やめる"}, 1) != 0) return;
    newPkg(); detFocus = detScroll = listScroll = 0; lastFolder.clear(); lastFile.clear();
    setStatus("新しいパッケージを作りました");
}
static void actQuit() {
    if (!P.dirty || dialog("終了", "保存していない変更があります。終了しますか?", {"終了", "やめる"}, 1) == 0) running = false;
}
struct MenuItem { const char* label; void (*fn)(); };
static const MenuItem MENUS[] = {{"新規", actNew}, {"スキン追加", actAddSkin}, {"保存して配置", actSave}, {"終了", actQuit}};
static const int NMENU = 4;

// ---------- 詳細ペインの行 ----------
struct Row {
    enum K { INFO, TITLE, FIELD, PROP, BTN, BTN_DANGER } k = INFO;
    std::string label, value;
    std::function<void()> onA, onX, onY, onLR;
    bool focusable() const { return k != INFO && k != TITLE; }
};
static std::string typeName(const pck::Asset& a) {
    if (isSkin(a)) return "スキン (type 0)";
    if (isCape(a)) return "マント (type 1)";
    if (a.type == 6) return "ローカライズ (type 6)";
    if (a.type == 4) return "ルート (type 4)";
    return "type " + std::to_string(a.type);
}
static void editText(int ai, const char* header, const std::string& cur, int maxLen, std::function<void(const std::string&)> apply, bool numeric = false) {
    std::string v;
    if (keyboard(header, cur, v, maxLen, numeric)) { if (ai < (int)P.a.size()) apply(v); }
}
static std::vector<Row> buildRows() {
    std::vector<Row> rows;
    if (sel < 0 || sel >= (int)P.a.size()) return rows;
    const int ai = sel;
    pck::Asset& a = P.a[ai];
    auto info = [&](const std::string& l, const std::string& v) { Row r; r.k = Row::INFO; r.label = l; r.value = v; rows.push_back(r); };
    auto field = [&](const std::string& l, const std::string& v, std::function<void()> onA) { Row r; r.k = Row::FIELD; r.label = l; r.value = v; r.onA = onA; rows.push_back(r); return &rows.back(); };
    info("ファイル名", a.name);
    info("種類 / サイズ", typeName(a) + " / " + std::to_string(a.data.size()) + " bytes" + (isPng(a) && a.data.size() > 0x7FFF ? "  (32767超)" : ""));
    if (ai == 0) field("パック名", packNameEff(), [ai] { editText(ai, "パック名", P.name, 30, [](const std::string& v) { P.name = v; markDirty(); }); });
    if (isSkin(a)) {
        field("名前", getProp(a, "DISPLAYNAME"), [ai] { editText(ai, "スキン名", getProp(P.a[ai], "DISPLAYNAME"), 30, [ai](const std::string& v) { if (!v.empty()) { setProp(P.a[ai], "DISPLAYNAME", v); markDirty(); } }); });
        field("テーマ名", getProp(a, "THEMENAME"), [ai] { editText(ai, "テーマ名 (空で削除)", getProp(P.a[ai], "THEMENAME"), 30, [ai](const std::string& v) { setTheme(P.a[ai], v); markDirty(); }); });
        field("スキンID", skinIds(a), [ai] { editText(ai, "スキンID (100000〜99999998)", skinIds(P.a[ai]), 8, [ai](const std::string& v) { setSkinId(ai, (uint32_t)strtoul(v.c_str(), nullptr, 10)); }, true); });
        bool slim = getProp(a, "ANIM") == "0x00080000";
        auto tg = [ai] { pck::Asset& s = P.a[ai]; setProp(s, "ANIM", getProp(s, "ANIM") == "0x00080000" ? "0x00040000" : "0x00080000"); markDirty(); };
        field("モデル", slim ? "スリム (Alex)" : "クラシック (Steve)", tg)->onLR = tg;
        field("スキン画像", "置き換える...", [ai] { Img im; if (pickImage("スキン画像を選ぶ (64x64 / 64x32)", fitsSkin, im)) replaceData(ai, im); });
        std::string cp = getProp(a, "CAPEPATH");
        Row* r = field("マント", cp.empty() ? "なし (Aで選ぶ)" : cp, [ai] { Img im; if (pickImage("マント画像を選ぶ (64x32)", fitsCape, im)) { int ni = setCape(ai, im); sel = ni; } });
        r->onX = [ai] { if (!getProp(P.a[ai], "CAPEPATH").empty()) removeCape(ai); };
    } else if (isPng(a)) {
        field("画像", "置き換える...", [ai] { Img im; if (pickImage("画像を選ぶ", anyPng, im)) replaceData(ai, im); });
    }
    if (a.type != 6) {
        Row t; t.k = Row::TITLE; t.label = "プロパティ (" + std::to_string(a.props.size()) + ")"; rows.push_back(t);
        for (size_t pi = 0; pi < a.props.size(); pi++) {
            Row r; r.k = Row::PROP; r.label = a.props[pi].key; r.value = a.props[pi].value;
            r.onA = [ai, pi] { if (ai < (int)P.a.size() && pi < P.a[ai].props.size()) editText(ai, "プロパティの値", P.a[ai].props[pi].value, 60, [ai, pi](const std::string& v) { if (pi < P.a[ai].props.size()) { P.a[ai].props[pi].value = v; markDirty(); } }); };
            r.onX = [ai, pi] { if (ai < (int)P.a.size() && pi < P.a[ai].props.size()) editText(ai, "プロパティのキー", P.a[ai].props[pi].key, 40, [ai, pi](const std::string& v) { if (!v.empty() && pi < P.a[ai].props.size()) { P.a[ai].props[pi].key = v; markDirty(); } }); };
            r.onY = [ai, pi] { if (ai < (int)P.a.size() && pi < P.a[ai].props.size()) { P.a[ai].props.erase(P.a[ai].props.begin() + pi); markDirty(); setStatus("プロパティを削除しました"); } };
            rows.push_back(r);
        }
        Row add; add.k = Row::BTN; add.label = "＋ プロパティを追加";
        add.onA = [ai] {
            std::string k, v;
            if (!keyboard("プロパティのキー", "", k, 40) || k.empty()) return;
            if (!keyboard("プロパティの値", "", v, 60)) return;
            if (ai < (int)P.a.size()) { P.a[ai].props.push_back({k, v}); markDirty(); }
        };
        rows.push_back(add);
    } else {
        Row t; t.k = Row::TITLE; t.label = "ローカライズは「名前」「テーマ名」「パック名」から自動で作られます"; rows.push_back(t);
    }
    if (isSkin(a)) { Row d; d.k = Row::BTN; d.label = "スキンを複製"; d.onA = [ai] { duplicateSkin(ai); }; rows.push_back(d); }
    if (ai >= 2) {
        Row d; d.k = Row::BTN_DANGER; d.label = "アセットを削除";
        d.onA = [ai] { if (dialog("削除", P.a[ai].name + " を削除しますか?", {"削除", "やめる"}, 1) == 0) deleteAsset(ai); };
        rows.push_back(d);
    }
    return rows;
}
static int firstFocusable(const std::vector<Row>& rows) { for (size_t i = 0; i < rows.size(); i++) if (rows[i].focusable()) return (int)i; return 0; }
static void selectAsset(int i) { if (i != sel) { sel = i; detFocus = 0; detScroll = 0; } }

// ---------- 描画 ----------
static void drawMenuBar() {
    fill(0, 0, 1280, MENU_H, BAR); fill(0, MENU_H - 1, 1280, 1, LINE);
    int x = 0;
    for (int i = 0; i < NMENU; i++) {
        int w = textW(SZ_M, MENUS[i].label) + 36;
        if (pane == MENU && menuFocus == i) { fill(x, 0, w, MENU_H - 1, LINE); outline(x + 2, 2, w - 4, MENU_H - 5, YEL); }
        text(SZ_M, MENUS[i].label, x + 18, MENU_H / 2 - 1, WHITE);
        hit(x, 0, w, MENU_H, [i] { menuFocus = i; MENUS[i].fn(); });
        x += w;
    }
    text(SZ_M, "NX PCK STUDIO", 1262 - textW(SZ_S, APP_VER) - 12, MENU_H / 2 - 1, YEL, 2);
    text(SZ_S, APP_VER, 1262, MENU_H / 2 + 2, MUTED, 2);
}
static int badge(int x, int cy, const std::string& s) {
    int w = textW(SZ_S, s) + 20;
    fill(x, cy - 12, w, 24, C{255, 255, 255, 51});
    text(SZ_S, s, x + 10, cy, WHITE);
    return w + 10;
}
static void drawInfoBar() {
    fill(0, INFO_Y, 1280, INFO_H, GREEN);
    int cy = INFO_Y + INFO_H / 2, x = 14;
    x += badge(x, cy, "パック: " + clipText(SZ_S, packNameEff(), 260));
    x += badge(x, cy, "アセット: " + std::to_string(P.a.size()));
    x += badge(x, cy, cfg.little ? "LE (Switch)" : "BE");
    std::string dst = "出力: NX/DLC/" + (lastFolder.empty() ? std::string("<フォルダー名>") : lastFolder) + "/" + (lastFile.empty() ? std::string("<ファイル名>") : lastFile) + ".pck";
    text(SZ_S, clipText(SZ_S, dst, 1280 - x - 20), 1266, cy, WHITE, 2);
}
static void ensureVisible(int& off, int pos, int size, int view) { if (pos < off) off = pos; else if (pos + size > off + view) off = pos + size - view; }

static void drawSidebar() {
    fill(0, MAIN_Y, SB_W, FOOT_Y - MAIN_Y, SIDE); fill(SB_W - 1, MAIN_Y, 1, FOOT_Y - MAIN_Y, LINE);
    fill(0, MAIN_Y, SB_W, HDR_H, BAR); fill(0, MAIN_Y + HDR_H - 1, SB_W, 1, LINE);
    text(SZ_M, "アセット", 15, MAIN_Y + HDR_H / 2, WHITE);
    text(SZ_S, std::to_string(P.a.size()) + " 件", SB_W - 15, MAIN_Y + HDR_H / 2, MUTED, 2);
    if (pane == LIST) fill(0, MAIN_Y + HDR_H - 3, SB_W, 3, YEL);
    int n = (int)P.a.size();
    listScroll = std::max(0, std::min(listScroll, std::max(0, n * LIST_ROW - LIST_RECT.h)));
    SDL_Rect area = LIST_RECT;
    SDL_RenderSetClipRect(R, &area);
    for (int i = 0; i < n; i++) {
        const pck::Asset& a = P.a[i];
        int y = LIST_RECT.y + i * LIST_ROW - listScroll;
        if (y + LIST_ROW < LIST_RECT.y || y > LIST_RECT.y + LIST_RECT.h) continue;
        bool warn = isPng(a) && a.data.size() > 0x7FFF;
        fill(0, y, SB_W - 1, LIST_ROW, i == sel ? (warn ? C{0x7a, 0x2a, 0x2a} : SELBG) : (warn ? WARNBG : SIDE));
        if (i == sel) fill(0, y, 4, LIST_ROW, warn ? RED : GREEN);
        else if (warn) fill(0, y, 3, LIST_ROW, RED);
        fill(0, y + LIST_ROW - 1, SB_W - 1, 1, ROWLINE);
        C ic = isSkin(a) ? GREEN : isCape(a) ? BLUE : a.type == 6 ? C{0xc0, 0x80, 0x20} : C{0x70, 0x70, 0x70};
        const char* ch = isSkin(a) ? "S" : isCape(a) ? "C" : a.type == 6 ? "L" : a.type == 4 ? "0" : "?";
        fill(14, y + 12, 32, 32, ic); text(SZ_M, ch, 30, y + 28, WHITE, 1);
        text(SZ_M, clipText(SZ_M, a.name, SB_W - 70 - 14), 58, y + 18, TXT);
        std::string sub = isSkin(a) ? getProp(a, "DISPLAYNAME") + "  /  " : "";
        text(SZ_S, clipText(SZ_S, sub + std::to_string(a.data.size()) + " bytes", SB_W - 70 - 14), 58, y + 40, MUTED);
        if (pane == LIST && i == sel) outline(2, y + 1, SB_W - 5, LIST_ROW - 2, YEL);
        hitIn(area, 0, y, SB_W, LIST_ROW, [i] { selectAsset(i); pane = LIST; });
    }
    SDL_RenderSetClipRect(R, nullptr);
    int by = FOOT_Y - ACT_H;
    fill(0, by, SB_W - 1, ACT_H, SIDE); fill(0, by, SB_W - 1, 1, LINE);
    fill(12, by + 8, SB_W - 25, ACT_H - 16, GREEN);
    text(SZ_M, "＋ スキンを追加", SB_W / 2, by + ACT_H / 2, WHITE, 1);
    hit(12, by + 8, SB_W - 25, ACT_H - 16, [] { actAddSkin(); });
}

static void drawPreview() {
    int y0 = MAIN_Y, h = DET_Y - MAIN_Y;
    fill(CT_X, y0, CT_W, h, BG); fill(CT_X, DET_Y - 1, CT_W, 1, LINE);
    fill(CT_X, y0, CT_W, HDR_H, BAR); fill(CT_X, y0 + HDR_H - 1, CT_W, 1, LINE);
    text(SZ_M, "プレビュー", CT_X + 15, y0 + HDR_H / 2, WHITE);
    if (sel < 0 || sel >= (int)P.a.size()) return;
    const pck::Asset& a = P.a[sel];
    text(SZ_S, a.name, CT_X + CT_W - 15, y0 + HDR_H / 2, MUTED, 2);
    int cy = y0 + HDR_H, ch = h - HDR_H - 1;
    uint32_t w = 0, hh = 0;
    if (isPng(a) && pck::pngSize(a.data, w, hh)) {
        int ci = a.type == 0 ? findAsset(getProp(a, "CAPEPATH")) : -1;
        uint32_t cw = 0, chh = 0;
        bool hasCape = ci >= 0 && pck::pngSize(P.a[ci].data, cw, chh);
        if (isSkin(a)) {
            // 3Dプレビュー (左) + 2Dのスキン画像 / マント画像 (右)
            fill(PV3D.x, PV3D.y, PV3D.w, PV3D.h, C{0x22, 0x22, 0x22});
            skin3d::draw(R, assetTex(a.data), w, hh, hasCape ? assetTex(P.a[ci].data) : nullptr, a.props, PV3D, yaw3d, pitch3d, zoom3d);
            outline(PV3D.x, PV3D.y, PV3D.w, PV3D.h, LINE, 1);
            text(SZ_S, "ドラッグ/右スティック: 回転  R: 自動回転  ZL/ZR: 拡大縮小", PV3D.x + PV3D.w / 2, PV3D.y + PV3D.h - 12, MUTED, 1);
            int rx = CT_X + 490;
            drawTex(assetTex(a.data), w, hh, rx, cy + 8, 200, ch - 50);
            text(SZ_S, std::to_string(w) + "x" + std::to_string(hh) + "  " + std::to_string(a.data.size()) + "B", rx + 100, cy + ch - 18, a.data.size() > 0x7FFF ? RED_HI : MUTED, 1);
            if (hasCape) {
                drawTex(assetTex(P.a[ci].data), cw, chh, rx + 210, cy + 8, 190, ch - 50);
                text(SZ_S, "マント " + std::to_string(cw) + "x" + std::to_string(chh), rx + 305, cy + ch - 18, MUTED, 1);
            }
        } else {
            drawTex(assetTex(a.data), w, hh, CT_X + 20, cy + 8, CT_W - 40, ch - 40);
            text(SZ_S, std::to_string(w) + "x" + std::to_string(hh) + "  " + std::to_string(a.data.size()) + " bytes", CT_X + CT_W / 2, cy + ch - 14, a.data.size() > 0x7FFF ? RED_HI : MUTED, 1);
        }
    } else if (a.type == 6) {
        text(SZ_S, "キー", CT_X + 20, cy + 16, MUTED); text(SZ_S, "値 (全言語共通)", CT_X + 470, cy + 16, MUTED);
        fill(CT_X + 10, cy + 30, CT_W - 20, 1, LINE);
        for (size_t i = 0; i < locEntries.size() && i < 7; i++) {
            text(SZ_S, clipText(SZ_S, locEntries[i].first, 430), CT_X + 20, cy + 50 + (int)i * 30, LOCKEY);
            text(SZ_M, clipText(SZ_M, locEntries[i].second, 400), CT_X + 470, cy + 50 + (int)i * 30, TXT);
        }
        if (locEntries.size() > 7) text(SZ_S, "ほか " + std::to_string(locEntries.size() - 7) + " 件", CT_X + 20, cy + 50 + 7 * 30, MUTED);
    } else if (sel == 0) {
        text(SZ_L, "NX PCK STUDIO", CT_X + CT_W / 2, cy + 50, YEL, 1);
        text(SZ_M, "PACKID: " + getProp(a, "PACKID"), CT_X + CT_W / 2, cy + 100, CYAN, 1);
        int skins = 0; for (auto& x : P.a) if (isSkin(x)) skins++;
        text(SZ_M, skins ? "スキン " + std::to_string(skins) + " 個" : "「スキン追加」からスキンを入れてください", CT_X + CT_W / 2, cy + 150, MUTED, 1);
        text(SZ_S, "保存すると NX/DLC/<フォルダー名>/<ファイル名>.pck に置きます", CT_X + CT_W / 2, cy + 195, MUTED, 1);
    } else {
        text(SZ_M, "プレビューできません", CT_X + CT_W / 2, cy + ch / 2, MUTED, 1);
    }
}

static void drawDetails() {
    fill(CT_X, DET_Y, CT_W, FOOT_Y - DET_Y, SIDE);
    fill(CT_X, DET_Y, CT_W, HDR_H, BAR); fill(CT_X, DET_Y + HDR_H - 1, CT_W, 1, LINE);
    text(SZ_M, "アセット詳細", CT_X + 15, DET_Y + HDR_H / 2, WHITE);
    if (pane == DETAIL) fill(CT_X, DET_Y + HDR_H - 3, CT_W, 3, YEL);
    std::vector<Row> rows = buildRows();
    int n = (int)rows.size();
    if (detFocus >= n) detFocus = std::max(0, n - 1);
    detScroll = std::max(0, std::min(detScroll, std::max(0, n * DET_ROW - DET_RECT.h)));
    SDL_Rect area = DET_RECT;
    SDL_RenderSetClipRect(R, &area);
    const int LX = CT_X + 15, RX = CT_X + CT_W - 15;
    for (int i = 0; i < n; i++) {
        const Row& r = rows[i];
        int y = DET_RECT.y + i * DET_ROW - detScroll;
        if (y + DET_ROW < DET_RECT.y || y > DET_RECT.y + DET_RECT.h) continue;
        int cy = y + DET_ROW / 2;
        bool foc = pane == DETAIL && i == detFocus && r.focusable();
        if (r.k != Row::BTN && r.k != Row::BTN_DANGER) fill(CT_X, y + DET_ROW - 1, CT_W, 1, ROWLINE);
        switch (r.k) {
        case Row::INFO:
            text(SZ_M, r.label, LX, cy, YEL);
            text(SZ_M, clipText(SZ_M, r.value, CT_W - 200), LX + 180, cy, TXT);
            break;
        case Row::TITLE:
            text(SZ_M, r.label, LX, cy + 4, YEL);
            break;
        case Row::FIELD: {
            text(SZ_M, r.label, LX, cy, YEL);
            fill(LX + 180, y + 4, RX - LX - 180, DET_ROW - 8, BG); outline(LX + 180, y + 4, RX - LX - 180, DET_ROW - 8, foc ? YEL : LINE, foc ? 2 : 1);
            text(SZ_M, clipText(SZ_M, r.value, RX - LX - 210), LX + 192, cy, r.onLR ? WHITE : TXT);
            if (r.onLR) text(SZ_M, "◀ ▶", RX - 12, cy, MUTED, 2);
            hitIn(area, CT_X, y, CT_W, DET_ROW, [i, r] { pane = DETAIL; detFocus = i; if (r.onA) r.onA(); });
            break;
        }
        case Row::PROP: {
            int kx = LX, kw = 250, vx = LX + 260, vw = RX - 44 - vx;
            fill(kx, y + 4, kw, DET_ROW - 8, BG); outline(kx, y + 4, kw, DET_ROW - 8, foc ? YEL : LINE, 1);
            text(SZ_M, clipText(SZ_M, r.label, kw - 20), kx + 10, cy, CYAN);
            fill(vx, y + 4, vw, DET_ROW - 8, BG); outline(vx, y + 4, vw, DET_ROW - 8, foc ? YEL : LINE, foc ? 2 : 1);
            text(SZ_M, clipText(SZ_M, r.value, vw - 20), vx + 10, cy, ORANGE);
            fill(RX - 32, y + 4, 32, DET_ROW - 8, RED); text(SZ_M, "×", RX - 16, cy, WHITE, 1);
            hitIn(area, kx, y, kw, DET_ROW, [i, r] { pane = DETAIL; detFocus = i; if (r.onX) r.onX(); });
            hitIn(area, vx, y, vw, DET_ROW, [i, r] { pane = DETAIL; detFocus = i; if (r.onA) r.onA(); });
            hitIn(area, RX - 40, y, 44, DET_ROW, [i, r] { pane = DETAIL; detFocus = i; if (r.onY) r.onY(); });
            break;
        }
        case Row::BTN: case Row::BTN_DANGER: {
            bool dg = r.k == Row::BTN_DANGER;
            fill(LX, y + 4, 320, DET_ROW - 8, dg ? (foc ? RED_HI : RED) : (foc ? GREEN_HI : GREEN));
            text(SZ_M, r.label, LX + 160, cy, WHITE, 1);
            hitIn(area, LX, y, 320, DET_ROW, [i, r] { pane = DETAIL; detFocus = i; if (r.onA) r.onA(); });
            break;
        }
        }
        if (foc && r.k != Row::FIELD && r.k != Row::PROP) outline(LX - 4, y + 2, r.k == Row::BTN || r.k == Row::BTN_DANGER ? 328 : CT_W - 22, DET_ROW - 4, YEL);
    }
    SDL_RenderSetClipRect(R, nullptr);
}

static void drawFooter() {
    fill(0, FOOT_Y, 1280, FOOT_H, BAR); fill(0, FOOT_Y, 1280, 1, LINE);
    const char* hint = pane == LIST ? "↑↓:選択  A/→:詳細  X:スキン追加  L:メニュー  +:終了"
                     : pane == DETAIL ? "↑↓:選択  A:編集  X:キー編集/マント解除  Y:削除  B/←:一覧  L:メニュー"
                                      : "←→:選択  A:実行  B/L:戻る";
    int hw = textW(SZ_S, hint);
    text(SZ_S, hint, 1270, FOOT_Y + FOOT_H / 2, MUTED, 2);
    text(SZ_S, clipText(SZ_S, status, 1280 - hw - 50), 12, FOOT_Y + FOOT_H / 2, statusColor);
}

static void drawAll() {
    hits.clear();
    fill(0, 0, 1280, 720, BG);
    drawSidebar(); drawPreview(); drawDetails(); drawMenuBar(); drawInfoBar(); drawFooter();
}

// ---------- 入力 ----------
static void handleInput(const Input& in) {
    if (in.tap) dispatchTap(in.tx, in.ty);
    if (!running) return;
    bool inPv = in.sx >= PV3D.x && in.sx < PV3D.x + PV3D.w && in.sy >= PV3D.y && in.sy < PV3D.y + PV3D.h;
    if (inPv && (in.dragDx || in.dragDy)) { yaw3d -= in.dragDx * 0.012f; pitch3d = std::max(-1.2f, std::min(1.2f, pitch3d + in.dragDy * 0.008f)); autoRot = false; }
    if (std::abs(in.stx) > 6000) { yaw3d += in.stx / 32767.f * 0.05f; autoRot = false; }
    if (std::abs(in.sty) > 6000) pitch3d = std::max(-1.2f, std::min(1.2f, pitch3d - in.sty / 32767.f * 0.03f));
    if (autoRot) yaw3d += 0.01f;
    if (in.down & HidNpadButton_R) autoRot = !autoRot;
    if (in.down & HidNpadButton_ZR) zoom3d = std::min(2.5f, zoom3d + 0.15f);
    if (in.down & HidNpadButton_ZL) zoom3d = std::max(0.5f, zoom3d - 0.15f);
    if (in.dragDy && !inPv) {
        if (in.sx >= LIST_RECT.x && in.sx < LIST_RECT.x + LIST_RECT.w && in.sy >= LIST_RECT.y && in.sy < LIST_RECT.y + LIST_RECT.h) listScroll -= in.dragDy;
        else if (in.sx >= DET_RECT.x && in.sy >= DET_RECT.y) detScroll -= in.dragDy;
    }
    uint64_t d = in.down;
    bool up = d & HidNpadButton_Up, down = d & HidNpadButton_Down, left = d & HidNpadButton_Left, right = d & HidNpadButton_Right;
    bool a = d & HidNpadButton_A, b = d & HidNpadButton_B, x = d & HidNpadButton_X, y = d & HidNpadButton_Y;
    if (d & HidNpadButton_Plus) { actQuit(); return; }
    if (d & HidNpadButton_L) { if (pane == MENU) pane = prevPane; else { prevPane = pane; pane = MENU; } return; }
    int n = (int)P.a.size();
    if (pane == MENU) {
        if (left) menuFocus = (menuFocus + NMENU - 1) % NMENU;
        if (right) menuFocus = (menuFocus + 1) % NMENU;
        if (b) pane = prevPane;
        if (a) { pane = prevPane; MENUS[menuFocus].fn(); }
    } else if (pane == LIST) {
        if (n && up) selectAsset((sel + n - 1) % n);
        if (n && down) selectAsset((sel + 1) % n);
        if (up || down) ensureVisible(listScroll, sel * LIST_ROW, LIST_ROW, LIST_RECT.h);
        if (right || a) { pane = DETAIL; detFocus = firstFocusable(buildRows()); detScroll = 0; }
        if (x) actAddSkin();
    } else {
        std::vector<Row> rows = buildRows();
        int rn = (int)rows.size();
        if (up || down) {
            int i = detFocus;
            for (int k = 0; k < rn; k++) { i = (i + (down ? 1 : rn - 1)) % rn; if (rows[i].focusable()) break; }
            detFocus = i;
            ensureVisible(detScroll, detFocus * DET_ROW, DET_ROW, DET_RECT.h);
            if (detFocus == firstFocusable(rows)) detScroll = 0;
        }
        if (detFocus < rn) {
            const Row& r = rows[detFocus];
            if ((left || right) && r.onLR) r.onLR();
            else if (left || b) pane = LIST;
            if (a && r.onA) { auto f = r.onA; f(); }
            if (x && r.onX) { auto f = r.onX; f(); }
            if (y && r.onY) { auto f = r.onY; f(); }
        } else if (left || b) pane = LIST;
    }
}

int main() {
    romfsInit();
    plInitialize(PlServiceType_User);
    SDL_Init(SDL_INIT_VIDEO);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    IMG_Init(IMG_INIT_PNG);
    TTF_Init();
    SDL_Window* win = SDL_CreateWindow("NX PCK Studio", 0, 0, 1280, 720, SDL_WINDOW_FULLSCREEN);
    R = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    loadFonts();

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();

    util::mkdirs(util::BASE);
    util::mkdirs(imgDir());
    cfg = util::loadConfig();
    newPkg();
    if (scanImages().empty()) {  // 初回: サンプルのスキン/マントを画像フォルダーに入れる
        for (const char* n : {"sample_skin.png", "sample_cape.png"}) {
            std::vector<uint8_t> d;
            if (util::readFile(std::string("romfs:/samples/") + n, d)) util::writeAtomic(imgDir() + n, d);
        }
        setStatus("サンプル画像を画像フォルダーに入れました");
    } else setStatus("「スキン追加」から画像を選んでください");

    while (running && appletMainLoop()) {
        Input in;
        pollInput(in);
        handleInput(in);
        drawAll();
        SDL_RenderPresent(R);
    }

    // 終了処理: ライブラリより先にファイル/フォント/テクスチャを全て閉じる (romfsExit 後に開きっぱなしのファイルを残さない)
    for (auto& kv : tcache) SDL_DestroyTexture(kv.second.t);
    tcache.clear();
    for (auto& kv : atex) if (kv.second) SDL_DestroyTexture(kv.second);
    atex.clear();
    clearImgCache();
    closeFonts();
    SDL_DestroyRenderer(R);
    SDL_DestroyWindow(win);
    TTF_Quit(); IMG_Quit(); SDL_Quit();
    plExit();
    romfsExit();
    return 0;
}

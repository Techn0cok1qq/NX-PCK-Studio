// NX PCK Studio — スイッチ内のスキン/マント画像から .pck を作って romfs に置く
#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <dirent.h>
#include <algorithm>
#include <cstdlib>
#include <map>
#include "pck.hpp"
#include "util.hpp"

// ---------- 色 (PCK-studio-Web の CSS と同じ) ----------
struct C { uint8_t r, g, b, a = 255; };
static const C STONE{0xc6, 0xc6, 0xc6}, WHITE{255, 255, 255}, STONE_LO{0x55, 0x55, 0x55}, WELL{0x2b, 0x2b, 0x2b},
    WELL_LO{0x0f, 0x0f, 0x0f}, INK{0x3f, 0x3f, 0x3f}, MUTED{0xaa, 0xaa, 0xaa}, BTN{0x70, 0x70, 0x70},
    BTN_HI{0xb0, 0xb0, 0xb0}, BTN_LO{0x3a, 0x3a, 0x3a}, BTN_HOVER{0x7a, 0x86, 0xbd}, GREEN{0x4a, 0x8f, 0x29},
    GREEN_HI{0x86, 0xd6, 0x5a}, GREEN_LO{0x2a, 0x5a, 0x14}, GREEN_HOVER{0x5a, 0xa5, 0x34}, YELLOW{0xff, 0xff, 0x55},
    BLACK{0, 0, 0}, SHADOW{0x3f, 0x3f, 0x3f}, DANGER{0xff, 0x55, 0x55};

static SDL_Renderer* R;
static TTF_Font *fJP, *fSm, *fMC;

// ---------- 描画ヘルパ ----------
static void fill(int x, int y, int w, int h, C c) {
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_Rect r{x, y, w, h};
    SDL_RenderFillRect(R, &r);
}
static void box(int x, int y, int w, int h, C base, C hi, C lo, int t) {
    fill(x, y, w, h, base); fill(x, y, w, t, hi); fill(x, y, t, h, hi);
    fill(x, y + h - t, w, t, lo); fill(x + w - t, y, t, h, lo);
}
static void stone(int x, int y, int w, int h) { fill(x - 2, y - 2, w + 4, h + 4, BLACK); box(x, y, w, h, STONE, WHITE, STONE_LO, 3); }
static void well(int x, int y, int w, int h) { box(x, y, w, h, WELL, WELL_LO, WHITE, 3); }

static std::map<std::string, std::pair<SDL_Texture*, SDL_Point>> tcache;
static void text(TTF_Font* f, const std::string& s, int x, int y, C c, int anchor = 0, bool shadow = false) {
    if (s.empty()) return;
    if (shadow) text(f, s, x + 2, y + 2, SHADOW, anchor, false);
    std::string key = std::to_string((uintptr_t)f) + "|" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + "|" + s;
    auto it = tcache.find(key);
    if (it == tcache.end()) {
        if (tcache.size() > 500) { for (auto& kv : tcache) SDL_DestroyTexture(kv.second.first); tcache.clear(); }
        SDL_Surface* sf = TTF_RenderUTF8_Blended(f, s.c_str(), SDL_Color{c.r, c.g, c.b, 255});
        if (!sf) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(R, sf);
        SDL_Point sz{sf->w, sf->h};
        SDL_FreeSurface(sf);
        it = tcache.emplace(key, std::make_pair(t, sz)).first;
    }
    SDL_Rect d{x, y, it->second.second.x, it->second.second.y};
    if (anchor == 1) d.x -= d.w / 2; else if (anchor == 2) d.x -= d.w;
    SDL_RenderCopy(R, it->second.first, nullptr, &d);
}
static std::string clip(const std::string& s, size_t maxCp) {  // コードポイント単位で切る
    size_t cp = 0, i = 0;
    while (i < s.size()) {
        if (cp == maxCp) return s.substr(0, i) + "...";
        i++; while (i < s.size() && ((uint8_t)s[i] & 0xC0) == 0x80) i++; cp++;
    }
    return s;
}
static void button(int x, int y, int w, int h, const std::string& label, bool focus, bool green) {
    fill(x - 2, y - 2, w + 4, h + 4, BLACK);
    if (green) box(x, y, w, h, focus ? GREEN_HOVER : GREEN, GREEN_HI, GREEN_LO, 3);
    else       box(x, y, w, h, focus ? BTN_HOVER : BTN, BTN_HI, BTN_LO, 3);
    if (focus) { fill(x - 5, y - 5, w + 10, 3, YELLOW); fill(x - 5, y + h + 2, w + 10, 3, YELLOW); fill(x - 5, y - 5, 3, h + 10, YELLOW); fill(x + w + 2, y - 5, 3, h + 10, YELLOW); }
    text(fJP, label, x + w / 2, y + (h - 30) / 2, focus ? YELLOW : WHITE, 1, true);
}
static void background() {  // 土ブロック風のタイル
    for (int ty = 0; ty < 720; ty += 32)
        for (int tx = 0; tx < 1280; tx += 32) {
            uint32_t h = (tx * 73856093u) ^ (ty * 19349663u); h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            int v = 62 + (int)(h % 24);
            fill(tx, ty, 32, 32, C{(uint8_t)(v + 28), (uint8_t)(v + 4), (uint8_t)(v - 22)});
        }
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    fill(0, 0, 1280, 720, C{0, 0, 0, 107});
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
static void drawImg(const Img& im, int x, int y, int boxW, int boxH) {  // 整数倍でドット絵のまま拡大
    SDL_Texture* t = imgTex(im.path);
    if (!t || !im.w || !im.h) return;
    int s = std::max(1, (int)std::min(boxW / im.w, boxH / im.h));
    SDL_Rect d{x + (boxW - (int)im.w * s) / 2, y + (boxH - (int)im.h * s) / 2, (int)im.w * s, (int)im.h * s};
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

// ---------- 状態 ----------
enum Screen { MAIN, CREATE, PICK };
static Screen screen = MAIN;
static util::Config cfg;
static std::string status;
static C statusColor = YELLOW;

struct Form {
    std::string name, theme;
    uint32_t skinId = 0;
    bool slim = false;
    Img skin, cape;
    bool hasSkin = false, hasCape = false;
} F;

static int imgCount = -1;  // -1 = 再計算が必要 (メニューに戻るたびに更新)
static int mainFocus = 0, formFocus = 0, pickFocus = 0;
static bool pickCape = false;
static std::vector<Img> pickList;

static uint32_t rnd() {
    static uint64_t s = armGetSystemTick();
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(s >> 33);
}
static uint32_t newSkinId() { return 100000 + rnd() % 99899998; }
static void setStatus(const std::string& s, bool bad = false) { status = s; statusColor = bad ? DANGER : YELLOW; }

static bool keyboard(const char* header, const std::string& init, std::string& out, int maxLen, bool numeric) {
    SwkbdConfig k;
    if (R_FAILED(swkbdCreate(&k, 0))) return false;
    swkbdConfigMakePresetDefault(&k);
    swkbdConfigSetHeaderText(&k, header);
    swkbdConfigSetInitialText(&k, init.c_str());
    swkbdConfigSetStringLenMax(&k, maxLen);
    if (numeric) swkbdConfigSetType(&k, SwkbdType_NumPad);
    char buf[256] = {0};
    Result rc = swkbdShow(&k, buf, sizeof buf);
    swkbdClose(&k);
    if (R_FAILED(rc)) return false;
    out = buf;
    return true;
}

static std::string outDir() { return "sdmc:/atmosphere/contents/" + cfg.titleId + "/romfs/NX/" + cfg.folder + "/"; }

// ---------- PCK作成 ----------
static void doBuild() {
    if (!F.hasSkin) return setStatus("スキン画像を選んでください", true);
    if (F.name.empty()) return setStatus("スキン名を入力してください", true);
    if (F.skinId < 100000 || F.skinId > 99999998) return setStatus("スキンIDは 100000〜99999998 にしてください", true);

    pck::SkinParams p;
    p.name = F.name; p.theme = F.theme; p.skinId = F.skinId; p.slim = F.slim; p.little = cfg.little;
    p.packId = 1 + rnd() % 0xFFFFFE;
    if (!util::readFile(F.skin.path, p.skinPng)) return setStatus("スキン画像を読めません", true);
    if (F.hasCape && !util::readFile(F.cape.path, p.capePng)) return setStatus("マント画像を読めません", true);
    // 0x7FFF バイト超はゲームがクラッシュする恐れ (Webツールと同じ基準)
    if (p.skinPng.size() > 0x7FFF || p.capePng.size() > 0x7FFF) return setStatus("画像が 32767 バイトを超えています", true);

    std::vector<uint8_t> out = pck::makeSkinPack(p);
    std::string safe = util::sanitize(F.name);
    if (safe.find_first_not_of('_') == std::string::npos) safe = "skin";
    std::string dir = outDir(), path = dir + safe + "_" + std::to_string(F.skinId) + ".pck";
    util::mkdirs(dir);
    if (!util::writeAtomic(path, out)) return setStatus("書き込みに失敗しました (SDの空き容量を確認)", true);
    setStatus("配置しました: " + path.substr(5) + "  ※ゲームを再起動してください");
    F.skinId = newSkinId();
}

// ---------- 描画 ----------
static void frame();
static void present() { frame(); SDL_RenderPresent(R); }

static void drawHeader(const char* sub) {
    text(fMC, "NX PCK STUDIO", 48, 22, YELLOW, 0, true);
    text(fSm, sub, 1232, 44, WHITE, 2, true);
}
static void drawFooter(const char* hint) {
    text(fSm, status, 48, 654, statusColor, 0, true);
    text(fSm, hint, 1232, 684, WHITE, 2, true);
}

static void drawMain() {
    drawHeader("スキンPCKを作る  v1.0.0");
    stone(40, 100, 1200, 530);
    text(fJP, "操作を選んでください", 640, 130, INK, 1);
    button(390, 210, 500, 80, "スキンを作る", mainFocus == 0, true);
    button(390, 330, 500, 80, "終了", mainFocus == 1, false);
    text(fSm, "画像フォルダー: " + std::string(imgDir()).substr(5) + "  (" + std::to_string(imgCount < 0 ? (imgCount = (int)scanImages().size()) : imgCount) + " 枚)", 70, 560, INK);
    text(fSm, "出力先: " + outDir().substr(5), 70, 590, INK);
    text(fSm, "自分のスキン/マントのPNGは画像フォルダーに入れてください", 70, 530, INK);
    drawFooter("↑↓:選択  A:決定  +:終了");
}

static void formRow(int i, const char* label, const std::string& value, bool dim = false) {
    int y = 130 + i * 66;
    text(fJP, label, 70, y + 12, INK);
    well(330, y, 400, 52);
    text(fJP, value.empty() ? "(未設定)" : clip(value, 22), 346, y + 10, value.empty() || dim ? MUTED : WHITE);
    if (formFocus == i) { fill(324, y - 6, 412, 3, YELLOW); fill(324, y + 55, 412, 3, YELLOW); fill(324, y - 6, 3, 64, YELLOW); fill(733, y - 6, 3, 64, YELLOW); }
}

static void drawCreate() {
    drawHeader("スキンを作る");
    stone(40, 100, 1200, 540);
    formRow(0, "スキン名", F.name);
    formRow(1, "テーマ名(任意)", F.theme);
    formRow(2, "スキンID", std::to_string(F.skinId));
    formRow(3, "モデル", F.slim ? "スリム (Alex)" : "クラシック (Steve)");
    formRow(4, "スキン展開図", F.hasSkin ? F.skin.name : "");
    formRow(5, "マント画像", F.hasCape ? F.cape.name : "");
    button(330, 130 + 6 * 66, 400, 52, "作成して配置", formFocus == 6, true);

    text(fSm, "スキン", 800, 112, INK);
    well(790, 138, 280, 280);
    if (F.hasSkin) drawImg(F.skin, 800, 148, 260, 260);
    text(fSm, "マント", 800, 428, INK);
    well(790, 454, 280, 148);
    if (F.hasCape) drawImg(F.cape, 800, 462, 260, 132);
    drawFooter("↑↓:選択  A:決定/変更  ←→:モデル  X:ID再生成/マント解除  B:戻る");
}

static bool fits(const Img& im) {  // スキン: 64x64 / 64x32、マント: 64x32
    return pickCape ? (im.w == 64 && im.h == 32) : (im.w == 64 && (im.h == 64 || im.h == 32));
}

static void drawPick() {
    drawHeader(pickCape ? "マント画像を選ぶ" : "スキン展開図を選ぶ");
    stone(40, 100, 1200, 540);
    if (pickList.empty()) {
        text(fJP, "該当する画像がありません", 70, 140, INK);
        text(fSm, "sdmc:/switch/nxpckstudio/images/ に PNG を入れてください", 70, 190, INK);
    }
    int first = std::max(0, std::min(pickFocus - 4, (int)pickList.size() - 10));
    for (int r = 0; r < 10 && first + r < (int)pickList.size(); r++) {
        const Img& im = pickList[first + r];
        int y = 126 + r * 48; bool f = first + r == pickFocus;
        if (f) { fill(66, y - 4, 664, 46, YELLOW); }
        fill(70, y, 656, 38, WELL);
        text(fSm, clip(im.name, 34), 82, y + 6, f ? YELLOW : WHITE);
        text(fSm, std::to_string(im.w) + "x" + std::to_string(im.h), 716, y + 6, MUTED, 2);
    }
    well(790, 126, 400, 400);
    if (!pickList.empty()) {
        const Img& im = pickList[pickFocus];
        drawImg(im, 800, 136, 380, 380);
        text(fSm, std::to_string(im.size) + " bytes", 990, 540, INK, 1);
    }
    drawFooter("↑↓:選択  A:決定  B:戻る");
}

static void frame() {
    background();
    if (screen == MAIN) drawMain(); else if (screen == CREATE) drawCreate(); else drawPick();
}

// ---------- 入力 ----------
static void openPicker(bool cape) {
    pickCape = cape; pickFocus = 0; pickList.clear();
    for (auto& im : scanImages()) if (fits(im)) pickList.push_back(im);
    screen = PICK;
}

static bool handleInput(uint64_t d) {  // false で終了
    bool up = d & HidNpadButton_Up, down = d & HidNpadButton_Down, a = d & HidNpadButton_A, b = d & HidNpadButton_B,
         x = d & HidNpadButton_X, left = d & HidNpadButton_Left, right = d & HidNpadButton_Right;
    if (screen == MAIN) {
        if (up || down) mainFocus = 1 - mainFocus;
        if (d & HidNpadButton_Plus) return false;
        if (a) {
            if (mainFocus == 0) { screen = CREATE; if (!F.skinId) F.skinId = newSkinId(); }
            else return false;
        }
    } else if (screen == CREATE) {
        if (up) formFocus = (formFocus + 6) % 7;
        if (down) formFocus = (formFocus + 1) % 7;
        if (b) { screen = MAIN; imgCount = -1; }
        if (x && formFocus == 2) F.skinId = newSkinId();
        if (x && formFocus == 5) F.hasCape = false;
        if (formFocus == 3 && (left || right || a)) F.slim = !F.slim;
        if (a) {
            std::string v;
            if (formFocus == 0 && keyboard("スキン名", F.name, v, 30, false)) F.name = v;
            else if (formFocus == 1 && keyboard("テーマ名(空でもOK)", F.theme, v, 30, false)) F.theme = v;
            else if (formFocus == 2 && keyboard("スキンID (100000〜99999998)", std::to_string(F.skinId), v, 8, true)) F.skinId = (uint32_t)strtoul(v.c_str(), nullptr, 10);
            else if (formFocus == 4) openPicker(false);
            else if (formFocus == 5) openPicker(true);
            else if (formFocus == 6) doBuild();
        }
    } else {
        int n = (int)pickList.size();
        if (n && up) pickFocus = (pickFocus + n - 1) % n;
        if (n && down) pickFocus = (pickFocus + 1) % n;
        if (b) screen = CREATE;
        if (a && n) {
            if (pickCape) { F.cape = pickList[pickFocus]; F.hasCape = true; }
            else { F.skin = pickList[pickFocus]; F.hasSkin = true; }
            screen = CREATE;
        }
    }
    return true;
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

    // 日本語は本体の共有フォント、タイトルだけ minecraft.ttf
    PlFontData sf;
    plGetSharedFontByType(&sf, PlSharedFontType_Standard);
    fJP = TTF_OpenFontRW(SDL_RWFromMem(sf.address, sf.size), 0, 26);
    fSm = TTF_OpenFontRW(SDL_RWFromMem(sf.address, sf.size), 0, 20);
    fMC = TTF_OpenFont("romfs:/minecraft.ttf", 44);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    util::mkdirs(util::BASE);
    util::mkdirs(imgDir());
    cfg = util::loadConfig();
    if (scanImages().empty()) {  // 初回: サンプルのスキン/マントを画像フォルダーに入れる
        for (const char* n : {"sample_skin.png", "sample_cape.png"}) {
            std::vector<uint8_t> d;
            if (util::readFile(std::string("romfs:/samples/") + n, d)) util::writeAtomic(imgDir() + n, d);
        }
        setStatus("サンプル画像を画像フォルダーに入れました");
    }

    while (appletMainLoop()) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {}
        padUpdate(&pad);
        if (!handleInput(padGetButtonsDown(&pad))) break;
        present();
    }

    for (auto& kv : imgCache) if (kv.second) SDL_DestroyTexture(kv.second);
    TTF_Quit(); IMG_Quit(); SDL_DestroyRenderer(R); SDL_DestroyWindow(win); SDL_Quit();
    plExit(); romfsExit();
    return 0;
}

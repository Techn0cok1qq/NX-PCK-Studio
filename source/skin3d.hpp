// 3Dスキンプレビュー。PCK-studio-Web の Skin3D(2Dキャンバス描画)を SDL_RenderGeometry に移植したもの。
// 標準パーツ + BOX / OFFSET プロパティ + 第2レイヤー + マントに対応。正射影で回転・俯仰・拡大縮小。
#pragma once
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>
#include "pck.hpp"

namespace skin3d {

struct UV { float x, y, w, h; };
enum { F_FRONT, F_BACK, F_RIGHT, F_LEFT, F_TOP, F_BOTTOM };

struct Box {
    float cx, cy, cz, hx, hy, hz;  // 中心と半サイズ(ワールド: +Xがキャラの左、+Yが上、+Zが正面)
    UV uv[6];
    bool mirror = false;
    int tex = 0;  // 0 スキン / 1 マント
};

// Minecraft の BOX 展開図 (HTML の parseBoxText と同じ配置)
inline void boxUv(float u, float v, float sx, float sy, float sz, UV o[6]) {
    o[F_FRONT]  = {u + sz,          v + sz, sx, sy};
    o[F_BACK]   = {u + 2 * sz + sx, v + sz, sx, sy};
    o[F_RIGHT]  = {u,               v + sz, sz, sy};
    o[F_LEFT]   = {u + sz + sx,     v + sz, sz, sy};
    o[F_TOP]    = {u + sz,          v,      sx, sz};
    o[F_BOTTOM] = {u + sz + sx,     v,      sx, sz};
}

struct Cls { const char* name; float bx, by; int key; int layer; bool second; };
static const Cls CLS[] = {
    {"HEAD", 0, 20, 0, 0, false},   {"HEADWEAR", 0, 20, 0, 1, true},
    {"BODY", 0, 20, 1, 0, false},   {"JACKET", 0, 20, 1, 1, true},
    {"WAIST", 0, 20, -1, -1, true}, {"BELT", 0, 20, -1, -1, true},
    {"BODYARMOR", 0, 20, -1, -1, true}, {"BODYARMOR0", 0, 20, -1, -1, true}, {"BODYARMOR1", 0, 20, -1, -1, true},
    {"ARM0", 5, 18, 2, 0, false},   {"SLEEVE0", 5, 18, 2, 1, true},  {"ARMARMOR0", 5, 18, -1, -1, true},
    {"ARM1", -5, 18, 3, 0, false},  {"SLEEVE1", -5, 18, 3, 1, true}, {"ARMARMOR1", -5, 18, -1, -1, true},
    {"LEG0", 2, 8, 4, 0, false},    {"PANTS0", 2, 8, 4, 1, true},    {"LEGGING0", 2, 8, -1, -1, true},
    {"SOCK0", 2, 8, -1, -1, true},  {"BOOT0", 2, 8, -1, -1, true},
    {"LEG1", -2, 8, 5, 0, false},   {"PANTS1", -2, 8, 5, 1, true},   {"LEGGING1", -2, 8, -1, -1, true},
    {"SOCK1", -2, 8, -1, -1, true}, {"BOOT1", -2, 8, -1, -1, true},
};
static const char* STD_NAME[6][2] = {{"HEAD", "HEADWEAR"}, {"BODY", "JACKET"}, {"ARM0", "SLEEVE0"}, {"ARM1", "SLEEVE1"}, {"LEG0", "PANTS0"}, {"LEG1", "PANTS1"}};

inline const Cls* findCls(const std::string& n) { for (auto& c : CLS) if (n == c.name) return &c; return nullptr; }

struct Off { float x = 0, y = 0, z = 0; };

struct Model { std::vector<Box> boxes; };

inline Model build(const std::vector<pck::Prop>& props, bool legacy, bool hasCape) {
    Model m;
    // ---- プロパティ ----
    std::vector<std::pair<std::string, Off>> offs;
    std::vector<std::string> boxLines;
    float armW = 4;
    for (auto& p : props) {
        if (p.key == "BOX") {
            std::istringstream ls(p.value); std::string line;
            while (std::getline(ls, line)) if (line.find_first_not_of(" \t\r") != std::string::npos) boxLines.push_back(line);
        } else if (p.key == "OFFSET") {
            std::istringstream is(p.value); std::string n, ax, val;
            if (is >> n >> ax >> val) {
                Off* o = nullptr;
                for (auto& e : offs) if (e.first == n) o = &e.second;
                if (!o) { offs.push_back({n, Off()}); o = &offs.back().second; }
                float v = (float)atof(val.c_str());
                char c = ax.empty() ? 0 : (char)toupper((unsigned char)ax[0]);
                if (c == 'X') o->x = v; else if (c == 'Y') o->y = v; else if (c == 'Z') o->z = v;
            }
        } else if (p.key == "ANIM") {
            unsigned long fl = strtoul(p.value.c_str(), nullptr, 0);
            if (fl & 0x80000UL) armW = 3;  // SLIM_MODEL
        }
    }
    auto offOf = [&](const std::string& n) { for (auto& e : offs) if (e.first == n) return e.second; return Off(); };

    // ---- BOX ----
    bool covered[6][2] = {};
    for (auto& line : boxLines) {
        std::istringstream is(line); std::vector<std::string> t; std::string s;
        while (is >> s) t.push_back(s);
        if (t.size() < 8) continue;
        float px = (float)atof(t[1].c_str()), py = (float)atof(t[2].c_str()), pz = (float)atof(t[3].c_str());
        float sx = (float)atof(t[4].c_str()), sy = (float)atof(t[5].c_str()), sz = (float)atof(t[6].c_str());
        float u = (float)atof(t[7].c_str()), v = t.size() > 8 ? (float)atof(t[8].c_str()) : 0.f;
        float infl = t.size() > 9 ? (float)atof(t[9].c_str()) : 0.f;
        bool mir = t.size() > 10 && (t[10] == "1" || t[10] == "true");
        const Cls* c = findCls(t[0]);
        float bx = c ? c->bx : 0, by = c ? c->by : 0;
        Off o = offOf(t[0]);
        Box b;
        b.cx = px - bx + sx / 2 + o.x;
        b.cy = -py + by - sy / 2 - o.y;
        b.cz = -pz - sz / 2 - o.z;
        float ex = (c && c->second) ? 0.0625f * sx : infl;
        b.hx = sx / 2 + ex; b.hy = sy / 2 + ex; b.hz = sz / 2 + ex;
        boxUv(u, v, sx, sy, sz, b.uv);
        b.mirror = mir;
        if (mir) std::swap(b.uv[F_RIGHT], b.uv[F_LEFT]);
        m.boxes.push_back(b);
        if (c && c->key >= 0) covered[c->key][c->layer] = true;
    }

    // ---- 標準パーツ ----
    struct Std { float cx, cy, cz, sx, sy, sz; float u0, v0, u1, v1; bool mir0, hasOver; };
    Std st[6] = {
        {0, 24, 0, 8, 8, 8, 0, 0, 32, 0, false, true},
        {0, 14, 0, 8, 12, 4, 16, 16, 16, 32, false, !legacy},
        {-(4 + armW / 2), 14, 0, armW, 12, 4, 40, 16, 40, 32, false, !legacy},
        {(4 + armW / 2), 14, 0, armW, 12, 4, legacy ? 40.f : 32.f, legacy ? 16.f : 48.f, 48, 48, legacy, !legacy},
        {-2, 2, 0, 4, 12, 4, 0, 16, 0, 32, false, !legacy},
        {2, 2, 0, 4, 12, 4, legacy ? 0.f : 16.f, legacy ? 16.f : 48.f, 0, 48, legacy, !legacy},
    };
    for (int k = 0; k < 6; k++) {
        for (int layer = 0; layer < 2; layer++) {
            if (covered[k][layer]) continue;
            if (layer == 1 && !st[k].hasOver) continue;
            const Std& s = st[k];
            Off o = offOf(STD_NAME[k][layer]);
            Box b;
            b.cx = s.cx + o.x; b.cy = s.cy - o.y; b.cz = s.cz - o.z;
            float ex = layer ? 0.0625f * s.sx : 0.f;
            b.hx = s.sx / 2 + ex; b.hy = s.sy / 2 + ex; b.hz = s.sz / 2 + ex;
            boxUv(layer ? s.u1 : s.u0, layer ? s.v1 : s.v0, s.sx, s.sy, s.sz, b.uv);
            b.mirror = (layer == 0) && s.mir0;
            if (b.mirror) std::swap(b.uv[F_RIGHT], b.uv[F_LEFT]);
            m.boxes.push_back(b);
        }
    }

    // ---- マント (外側の面 = 背中側に 1,1 のタイル) ----
    if (hasCape) {
        Box b; b.cx = 0; b.cy = 12; b.cz = -2.5f; b.hx = 5; b.hy = 8; b.hz = 0.5f; b.tex = 1;
        boxUv(0, 0, 10, 16, 1, b.uv);
        std::swap(b.uv[F_FRONT], b.uv[F_BACK]);
        m.boxes.push_back(b);
    }
    return m;
}

struct Face { float p[4][2]; float uv[4][2]; float z; float shade; int tex; };

// area の中に描く。yaw/pitch はラジアン、zoom は 1.0 が標準。
inline void draw(SDL_Renderer* R, SDL_Texture* skin, uint32_t sw, uint32_t sh, SDL_Texture* cape,
                 const std::vector<pck::Prop>& props, SDL_Rect area, float yaw, float pitch, float zoom) {
    if (!skin || !sw || !sh) return;
    bool legacy = (sh * 2 == sw);
    float nu = 64.f, nv = legacy ? 32.f : 64.f;
    Model m = build(props, legacy, cape != nullptr);
    SDL_SetTextureBlendMode(skin, SDL_BLENDMODE_BLEND);
    if (cape) SDL_SetTextureBlendMode(cape, SDL_BLENDMODE_BLEND);

    float cy_ = cosf(yaw), sy_ = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    float scale = std::min(area.h / 38.f, area.w / 26.f) * zoom;
    float ox = area.x + area.w / 2.f, oy = area.y + area.h / 2.f;
    auto proj = [&](float x, float y, float z, float& X, float& Y, float& Z) {
        float x1 = x * cy_ + z * sy_, z1 = -x * sy_ + z * cy_;  // Y軸回転
        float y1 = (y - 12.f) * cp - z1 * sp, z2 = (y - 12.f) * sp + z1 * cp;  // X軸回転 (中心は体の中央)
        X = ox + x1 * scale; Y = oy - y1 * scale; Z = z2;
    };

    std::vector<Face> faces;
    for (const Box& b : m.boxes) {
        float x0 = b.cx - b.hx, x1 = b.cx + b.hx, y0 = b.cy - b.hy, y1 = b.cy + b.hy, z0 = b.cz - b.hz, z1 = b.cz + b.hz;
        // 各面の角: TL, TR, BR, BL (テクスチャのタイル上の向き)
        float C[6][4][3] = {
            {{x0, y1, z1}, {x1, y1, z1}, {x1, y0, z1}, {x0, y0, z1}},  // 正面 +Z
            {{x1, y1, z0}, {x0, y1, z0}, {x0, y0, z0}, {x1, y0, z0}},  // 背面 -Z
            {{x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {x0, y0, z0}},  // 右 -X
            {{x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}, {x1, y0, z1}},  // 左 +X
            {{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}},  // 上
            {{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}},  // 下
        };
        static const float SHADE[6] = {0.92f, 0.85f, 0.78f, 0.78f, 1.0f, 0.65f};
        for (int f = 0; f < 6; f++) {
            Face fc; fc.tex = b.tex; fc.shade = SHADE[f];
            float zs = 0;
            for (int i = 0; i < 4; i++) { float Z; proj(C[f][i][0], C[f][i][1], C[f][i][2], fc.p[i][0], fc.p[i][1], Z); zs += Z; }
            fc.z = zs / 4;
            float cr = (fc.p[1][0] - fc.p[0][0]) * (fc.p[2][1] - fc.p[1][1]) - (fc.p[1][1] - fc.p[0][1]) * (fc.p[2][0] - fc.p[1][0]);
            if (cr <= 0) continue;  // 裏向きは描かない
            const UV& r = b.uv[f];
            float dn = b.tex ? 32.f : nv, dnu = 64.f; (void)nu;
            float ua = r.x / dnu, ub = (r.x + r.w) / dnu, va = r.y / dn, vb = (r.y + r.h) / dn;
            float U[4] = {ua, ub, ub, ua}, V[4] = {va, va, vb, vb};
            if (b.mirror && f != F_RIGHT && f != F_LEFT) { std::swap(U[0], U[1]); std::swap(U[2], U[3]); }  // 前後上下は U 反転
            for (int i = 0; i < 4; i++) { fc.uv[i][0] = U[i]; fc.uv[i][1] = V[i]; }
            faces.push_back(fc);
        }
    }
    std::sort(faces.begin(), faces.end(), [](const Face& a, const Face& b) { return a.z < b.z; });  // 奥から手前へ

    SDL_RenderSetClipRect(R, &area);
    for (const Face& f : faces) {
        SDL_Texture* t = f.tex ? cape : skin;
        if (!t) continue;
        Uint8 sh8 = (Uint8)(255 * f.shade);
        SDL_Vertex v[4];
        for (int i = 0; i < 4; i++) {
            v[i].position = {f.p[i][0], f.p[i][1]};
            v[i].color = {sh8, sh8, sh8, 255};
            v[i].tex_coord = {f.uv[i][0], f.uv[i][1]};
        }
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(R, t, v, 4, idx, 6);
    }
    SDL_RenderSetClipRect(R, nullptr);
}

}  // namespace skin3d

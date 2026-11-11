// アニメーションスキン / マント生成 (Animation-Cape-SkinPack-Generator の script.js の移植)
//  - GIF のフレーム分割 (LZW / 透過 / disposal)
//  - テンプレート (TemplateCapes.png) への合成と 35KB 以下への縮小
//  - 384x192 のマント用テクスチャ作成
//  - PNG の書き出し (zlib)
// SDL に依存しない (テンプレート PNG の読み込みだけは呼び出し側で行う)
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>
#include <zlib.h>

namespace anim {

// script.js の TARGET と同じ値
static const int TX = 6, TY = 6, TW = 60, TH = 96;
static const size_t MAX_BYTES = 35 * 1024;
static const int CAPE_W = 384, CAPE_H = 192;

// ---------------- PNG 書き出し (RGBA 8bit) ----------------
inline void be32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 3; i >= 0; i--) o.push_back((v >> (8 * i)) & 255); }
inline void chunk(std::vector<uint8_t>& o, const char* type, const uint8_t* d, size_t n) {
    be32(o, (uint32_t)n);
    size_t st = o.size();
    o.insert(o.end(), type, type + 4);
    if (n) o.insert(o.end(), d, d + n);
    uLong c = crc32(0L, o.data() + st, (uInt)(4 + n));
    be32(o, (uint32_t)c);
}
// 行ごとに None / Sub / Up のうち絶対値の合計が小さいフィルターを選ぶ
inline std::vector<uint8_t> encodePng(const uint8_t* rgba, int w, int h) {
    const size_t stride = (size_t)w * 4;
    std::vector<uint8_t> raw((stride + 1) * h), f1(stride), f2(stride);
    for (int y = 0; y < h; y++) {
        const uint8_t* cur = rgba + y * stride;
        const uint8_t* up = y ? cur - stride : nullptr;
        long s0 = 0, s1 = 0, s2 = 0;
        for (size_t i = 0; i < stride; i++) {
            uint8_t left = i >= 4 ? cur[i - 4] : 0, u = up ? up[i] : 0;
            f1[i] = (uint8_t)(cur[i] - left);
            f2[i] = (uint8_t)(cur[i] - u);
            s0 += cur[i] < 128 ? cur[i] : 256 - cur[i];
            s1 += f1[i] < 128 ? f1[i] : 256 - f1[i];
            s2 += f2[i] < 128 ? f2[i] : 256 - f2[i];
        }
        uint8_t* dst = raw.data() + y * (stride + 1);
        if (s1 < s0 && s1 <= s2) { dst[0] = 1; memcpy(dst + 1, f1.data(), stride); }
        else if (s2 < s0) { dst[0] = 2; memcpy(dst + 1, f2.data(), stride); }
        else { dst[0] = 0; memcpy(dst + 1, cur, stride); }
    }
    uLongf zn = compressBound((uLong)raw.size());
    std::vector<uint8_t> z(zn);
    if (compress2(z.data(), &zn, raw.data(), (uLong)raw.size(), 9) != Z_OK) return {};
    z.resize(zn);
    std::vector<uint8_t> o = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    be32(ihdr, (uint32_t)w); be32(ihdr, (uint32_t)h);
    ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk(o, "IHDR", ihdr.data(), ihdr.size());
    chunk(o, "IDAT", z.data(), z.size());
    chunk(o, "IEND", nullptr, 0);
    return o;
}

// ---------------- GIF デコード ----------------
// LZW 展開。index 列を out に入れる (expected 個で打ち切り)
inline void lzw(const std::vector<uint8_t>& data, int minCode, size_t expected, std::vector<uint8_t>& out) {
    out.clear();
    if (minCode < 2 || minCode > 11) return;
    const int clear = 1 << minCode, eoi = clear + 1;
    int next = eoi + 1, size = minCode + 1, prev = -1, first = 0;
    static uint16_t prefix[4096];
    static uint8_t suffix[4096], stack[4100];
    uint32_t bits = 0; int nb = 0; size_t pos = 0;
    out.reserve(expected);
    while (out.size() < expected) {
        while (nb < size && pos < data.size()) { bits |= (uint32_t)data[pos++] << nb; nb += 8; }
        if (nb < size) break;
        int code = (int)(bits & ((1u << size) - 1)); bits >>= size; nb -= size;
        if (code == clear) { size = minCode + 1; next = eoi + 1; prev = -1; continue; }
        if (code == eoi) break;
        if (prev == -1) {
            if (code >= clear) break;
            out.push_back((uint8_t)code); prev = code; first = code;
            continue;
        }
        int cur = code, sp = 0;
        if (code >= next) { if (code > next) break; stack[sp++] = (uint8_t)first; cur = prev; }
        while (cur >= clear) { stack[sp++] = suffix[cur]; cur = prefix[cur]; }
        first = cur; stack[sp++] = (uint8_t)cur;
        while (sp) out.push_back(stack[--sp]);
        if (next < 4096) {
            prefix[next] = (uint16_t)prev; suffix[next] = (uint8_t)first; next++;
            if (next == (1 << size) && size < 12) size++;
        }
        prev = code;
    }
    if (out.size() > expected) out.resize(expected);
}

// 各フレームを (disposal を反映して) 合成した全面 RGBA で渡す。cb が false を返すと中断。
// 戻り値: 読めたフレーム数 (形式エラーなら -1)
inline int decodeGif(const std::vector<uint8_t>& g, const std::function<bool(const uint8_t*, int, int)>& cb, int* outW = nullptr, int* outH = nullptr) {
    if (g.size() < 13 || memcmp(g.data(), "GIF8", 4) != 0) return -1;
    size_t p = 6;
    int W = g[p] | (g[p + 1] << 8), H = g[p + 2] | (g[p + 3] << 8);
    uint8_t flags = g[p + 4];
    p += 7;
    if (W <= 0 || H <= 0 || (size_t)W * H > 16u * 1024 * 1024) return -1;
    if (outW) *outW = W;
    if (outH) *outH = H;
    std::vector<uint8_t> gct;
    if (flags & 0x80) {
        size_t n = 3u << ((flags & 7) + 1);
        if (p + n > g.size()) return -1;
        gct.assign(g.begin() + p, g.begin() + p + n); p += n;
    }
    std::vector<uint8_t> canvas((size_t)W * H * 4, 0), saved;
    int disposal = 0, transp = -1, count = 0;
    auto skipSub = [&](size_t& q) { while (q < g.size() && g[q]) q += 1 + g[q]; q++; };
    while (p < g.size()) {
        uint8_t b = g[p++];
        if (b == 0x3B) break;
        if (b == 0x21) {
            if (p >= g.size()) break;
            uint8_t label = g[p++];
            if (label == 0xF9 && p + 5 < g.size() && g[p] >= 4) {
                uint8_t pk = g[p + 1];
                disposal = (pk >> 2) & 7;
                transp = (pk & 1) ? g[p + 4] : -1;
            }
            skipSub(p);
        } else if (b == 0x2C) {
            if (p + 9 > g.size()) break;
            int fx = g[p] | (g[p + 1] << 8), fy = g[p + 2] | (g[p + 3] << 8), fw = g[p + 4] | (g[p + 5] << 8), fh = g[p + 6] | (g[p + 7] << 8);
            uint8_t pk = g[p + 8]; p += 9;
            std::vector<uint8_t> lct;
            if (pk & 0x80) {
                size_t n = 3u << ((pk & 7) + 1);
                if (p + n > g.size()) break;
                lct.assign(g.begin() + p, g.begin() + p + n); p += n;
            }
            if (p >= g.size()) break;
            int minCode = g[p++];
            std::vector<uint8_t> comp;
            while (p < g.size() && g[p]) {
                size_t n = g[p];
                if (p + 1 + n > g.size()) n = g.size() - p - 1;
                comp.insert(comp.end(), g.begin() + p + 1, g.begin() + p + 1 + n);
                p += 1 + g[p];
            }
            p++;
            std::vector<uint8_t> idx;
            lzw(comp, minCode, (size_t)fw * fh, idx);
            const std::vector<uint8_t>& pal = lct.empty() ? gct : lct;
            if (disposal == 3) saved = canvas;
            if (fw > 0 && fh > 0) {
                std::vector<int> rowMap(fh);
                if (pk & 0x40) {
                    int r = 0;
                    for (int pass = 0; pass < 4; pass++) {
                        static const int st[4] = {0, 4, 2, 1}, sp[4] = {8, 8, 4, 2};
                        for (int y = st[pass]; y < fh; y += sp[pass]) rowMap[r++] = y;
                    }
                } else for (int y = 0; y < fh; y++) rowMap[y] = y;
                for (int r = 0; r < fh; r++) {
                    int dy = fy + rowMap[r];
                    if (dy < 0 || dy >= H) continue;
                    for (int x = 0; x < fw; x++) {
                        size_t k = (size_t)r * fw + x;
                        if (k >= idx.size()) break;
                        int dx = fx + x;
                        if (dx < 0 || dx >= W) continue;
                        int ci = idx[k];
                        if (ci == transp || (size_t)ci * 3 + 2 >= pal.size()) continue;
                        uint8_t* d = &canvas[((size_t)dy * W + dx) * 4];
                        d[0] = pal[ci * 3]; d[1] = pal[ci * 3 + 1]; d[2] = pal[ci * 3 + 2]; d[3] = 255;
                    }
                }
            }
            count++;
            if (!cb(canvas.data(), W, H)) return count;
            if (disposal == 2) {
                for (int y = fy; y < fy + fh && y < H; y++)
                    for (int x = fx; x < fx + fw && x < W; x++)
                        if (y >= 0 && x >= 0) memset(&canvas[((size_t)y * W + x) * 4], 0, 4);
            } else if (disposal == 3 && !saved.empty()) canvas = saved;
            disposal = 0; transp = -1;
        } else break;
    }
    return count;
}

// ---------------- 合成 ----------------
// 面積平均 (premultiplied) で拡縮
inline std::vector<uint8_t> areaScale(const uint8_t* s, int sw, int sh, int dw, int dh) {
    std::vector<uint8_t> o((size_t)dw * dh * 4, 0);
    for (int y = 0; y < dh; y++) {
        double y0 = (double)y * sh / dh, y1 = (double)(y + 1) * sh / dh;
        for (int x = 0; x < dw; x++) {
            double x0 = (double)x * sw / dw, x1 = (double)(x + 1) * sw / dw;
            double r = 0, gg = 0, b = 0, a = 0, wsum = 0;
            for (int iy = (int)y0; iy < (int)std::ceil(y1) && iy < sh; iy++) {
                double wy = std::min(y1, (double)iy + 1) - std::max(y0, (double)iy);
                for (int ix = (int)x0; ix < (int)std::ceil(x1) && ix < sw; ix++) {
                    double w = (std::min(x1, (double)ix + 1) - std::max(x0, (double)ix)) * wy;
                    const uint8_t* p = s + ((size_t)iy * sw + ix) * 4;
                    double al = p[3] / 255.0;
                    r += p[0] * al * w; gg += p[1] * al * w; b += p[2] * al * w; a += al * w; wsum += w;
                }
            }
            uint8_t* d = &o[((size_t)y * dw + x) * 4];
            if (a > 0 && wsum > 0) {
                d[0] = (uint8_t)std::min(255.0, r / a + 0.5); d[1] = (uint8_t)std::min(255.0, gg / a + 0.5); d[2] = (uint8_t)std::min(255.0, b / a + 0.5);
                d[3] = (uint8_t)std::min(255.0, a / wsum * 255.0 + 0.5);
            }
        }
    }
    return o;
}
// src-over で dst(tw x th) の (ox,oy) に重ねる
inline void blendOver(std::vector<uint8_t>& dst, int tw, int th, const std::vector<uint8_t>& src, int sw, int sh, int ox, int oy) {
    for (int y = 0; y < sh; y++) {
        if (oy + y < 0 || oy + y >= th) continue;
        for (int x = 0; x < sw; x++) {
            if (ox + x < 0 || ox + x >= tw) continue;
            const uint8_t* s = &src[((size_t)y * sw + x) * 4];
            uint8_t* d = &dst[((size_t)(oy + y) * tw + ox + x) * 4];
            double sa = s[3] / 255.0, da = d[3] / 255.0, oa = sa + da * (1 - sa);
            if (oa <= 0) { d[0] = d[1] = d[2] = d[3] = 0; continue; }
            for (int c = 0; c < 3; c++) d[c] = (uint8_t)std::min(255.0, (s[c] * sa + d[c] * da * (1 - sa)) / oa + 0.5);
            d[3] = (uint8_t)(oa * 255.0 + 0.5);
        }
    }
}
// encodeFrame: テンプレートに GIF の1フレームを重ね、PNG が 35KB 以下になるまで 1.0 → 0.3 倍に縮める。
// frame には合成後 (テンプレートと同じ大きさ) の RGBA が入る
inline void encodeFrame(const std::vector<uint8_t>& tpl, int tw, int th, const uint8_t* src, int sw, int sh, std::vector<uint8_t>& frame) {
    for (int step = 10; step >= 3; step--) {
        frame = tpl;
        int dw = std::max(1, (int)std::lround(TW * step / 10.0)), dh = std::max(1, (int)std::lround(TH * step / 10.0));
        std::vector<uint8_t> sc = areaScale(src, sw, sh, dw, dh);
        blendOver(frame, tw, th, sc, dw, dh, TX, TY);
        std::vector<uint8_t> png = encodePng(frame.data(), tw, th);
        if (png.size() <= MAX_BYTES || step == 3) return;
    }
}

// makeCapeTexture: 合成フレームの (6,6,60,96) から 384x192 のマント画像 (RGBA) を作る
inline std::vector<uint8_t> capeTexture(const std::vector<uint8_t>& frame, int fw, int fh) {
    std::vector<uint8_t> o((size_t)CAPE_W * CAPE_H * 4, 0);
    auto region = [&](int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh) {   // 補間なし (imageSmoothingEnabled = false)
        for (int j = 0; j < dh; j++)
            for (int i = 0; i < dw; i++) {
                int x = sx + i * sw / dw, y = sy + j * sh / dh;
                if (x >= fw || y >= fh || dx + i >= CAPE_W || dy + j >= CAPE_H) continue;
                memcpy(&o[((size_t)(dy + j) * CAPE_W + dx + i) * 4], &frame[((size_t)y * fw + x) * 4], 4);
            }
    };
    region(TX, TY, 60, 96, 6, 6, 60, 96);
    region(TX, TY, 60, 96, 72, 6, 60, 96);
    region(TX, TY, 60, 1, 6, 0, 60, 6);
    region(TX, TY + 95, 60, 1, 66, 0, 60, 6);
    region(TX, TY, 1, 96, 0, 6, 6, 96);
    region(TX + 59, TY, 1, 96, 66, 6, 6, 96);
    return o;
}

// 自然順 (skin_2 < skin_10) の比較。大文字小文字は区別しない
inline bool naturalLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (isdigit((unsigned char)a[i]) && isdigit((unsigned char)b[j])) {
            size_t si = i, sj = j;
            while (si < a.size() && a[si] == '0') si++;
            while (sj < b.size() && b[sj] == '0') sj++;
            size_t ei = si, ej = sj;
            while (ei < a.size() && isdigit((unsigned char)a[ei])) ei++;
            while (ej < b.size() && isdigit((unsigned char)b[ej])) ej++;
            if (ei - si != ej - sj) return ei - si < ej - sj;
            int c = a.compare(si, ei - si, b, sj, ej - sj);
            if (c) return c < 0;
            i = ei; j = ej;
        } else {
            int ca = tolower((unsigned char)a[i]), cb = tolower((unsigned char)b[j]);
            if (ca != cb) return ca < cb;
            i++; j++;
        }
    }
    return a.size() - i < b.size() - j;
}

}  // namespace anim

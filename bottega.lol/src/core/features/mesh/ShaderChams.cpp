#include "ShaderChams.h"
#include "imgui_internal.h"
#include "MeshParser.h"
#include "sdk/MeshBridge.h"
#include <algorithm>
#include <cmath>

namespace Cheat {
namespace Visuals {
namespace ShaderChams {
namespace {

enum class AnimKind : int {
    ScanY = 0,
    ScanX,
    Cross,
    DualScan,
    Pulse,
    Flicker,
    Stripes,
    Rain,
    Wave,
    Rise,
    Ripple,
    Glitch,
    Sparkle,
    Diagonal,
    RainbowFlow,
};

struct Palette {
    ImU32 top, bot, band, band_core, outline;
};

struct StyleDef {
    AnimKind anim;
    float speed;
    float band_frac;
    float fresnel;
    Palette pal;
};

static const char* k_names[] = {
    "plasma pulse", "ember rise", "toxin fog", "frost coat", "void gate",
    "neon cross", "gold dust", "matrix rain", "aurora drift", "blood mist",
    "ocean swell", "holo scan", "sunset glide", "ghost sheet", "chroma wave",
};
static_assert(sizeof(k_names) / sizeof(k_names[0]) == StyleCount, "style names");

ImU32 MulAlpha(ImU32 c, float a)
{
	int na = (int)((float)((c >> IM_COL32_A_SHIFT) & 0xFF) * a + 0.5f);
	if (na < 0) na = 0;
	if (na > 255) na = 255;
	return (c & ~IM_COL32_A_MASK) | ((ImU32)na << IM_COL32_A_SHIFT);
}

ImU32 LerpCol(ImU32 a, ImU32 b, float t)
{
	if (t < 0.f) t = 0.f;
	if (t > 1.f) t = 1.f;

	ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a);
	ImVec4 cb = ImGui::ColorConvertU32ToFloat4(b);
	return ImGui::ColorConvertFloat4ToU32(ImVec4(
		ca.x + (cb.x - ca.x) * t,
		ca.y + (cb.y - ca.y) * t,
		ca.z + (cb.z - ca.z) * t,
		ca.w + (cb.w - ca.w) * t));
}

ImU32 HSVA(float h, float s, float v, float a)
{
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(std::fmod(h, 1.0f), s, v, r, g, b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
}

float Hash11(float n)
{
    const float s = std::sin(n * 127.1f) * 43758.5453f;
    return s - std::floor(s);
}

Palette AimPalette()
{
    return {
        IM_COL32(255, 90, 90, 130),
        IM_COL32(140, 20, 30, 100),
        IM_COL32(255, 180, 180, 150),
        IM_COL32(255, 230, 230, 200),
        IM_COL32(255, 70, 70, 235),
    };
}

// цвета/аним под каждый стиль
StyleDef DefFor(int style)
{
	switch (style)
	{
	case Ember:     return { AnimKind::Rise,        1.55f, 0.14f, 0.70f, {
		IM_COL32(255,170,60,130), IM_COL32(160,30,10,105),
		IM_COL32(255,220,120,160), IM_COL32(255,250,200,220), IM_COL32(255,140,50,235) }};
	case Toxic:     return { AnimKind::Rain,        1.25f, 0.10f, 0.50f, {
		IM_COL32(160,255,70,120), IM_COL32(20,90,20,100),
		IM_COL32(200,255,120,150), IM_COL32(230,255,180,210), IM_COL32(140,255,80,230) }};
	case Ice:       return { AnimKind::Ripple,      1.10f, 0.08f, 0.85f, {
		IM_COL32(220,245,255,140), IM_COL32(80,140,220,95),
		IM_COL32(200,240,255,150), IM_COL32(255,255,255,210), IM_COL32(180,230,255,235) }};
	case Void:      return { AnimKind::Flicker,     2.40f, 0.20f, 0.45f, {
		IM_COL32(160,80,255,120), IM_COL32(20,8,40,110),
		IM_COL32(200,140,255,140), IM_COL32(240,200,255,190), IM_COL32(180,100,255,230) }};
	case Neon:      return { AnimKind::Cross,       1.70f, 0.12f, 0.60f, {
		IM_COL32(255,60,200,125), IM_COL32(40,220,255,105),
		IM_COL32(255,180,255,160), IM_COL32(255,255,255,220), IM_COL32(255,80,220,235) }};
	case Gold:      return { AnimKind::Pulse,       0.95f, 0.20f, 0.80f, {
		IM_COL32(255,220,100,135), IM_COL32(140,80,20,105),
		IM_COL32(255,240,160,150), IM_COL32(255,255,220,210), IM_COL32(255,200,80,235) }};
	case Matrix:    return { AnimKind::Stripes,     2.10f, 0.045f, 0.35f, {
		IM_COL32(40,220,80,100), IM_COL32(5,40,10,95),
		IM_COL32(80,255,120,170), IM_COL32(180,255,180,220), IM_COL32(60,255,100,230) }};
	case Aurora:    return { AnimKind::Wave,        0.85f, 0.18f, 0.70f, {
		IM_COL32(80,255,190,120), IM_COL32(180,80,255,105),
		IM_COL32(160,255,230,150), IM_COL32(230,255,255,200), IM_COL32(140,255,210,230) }};
	case Blood:     return { AnimKind::Glitch,      1.80f, 0.10f, 0.50f, {
		IM_COL32(220,40,50,130), IM_COL32(60,5,10,110),
		IM_COL32(255,90,90,150), IM_COL32(255,180,180,200), IM_COL32(220,40,50,235) }};
	case Ocean:     return { AnimKind::ScanX,       0.90f, 0.24f, 0.65f, {
		IM_COL32(60,200,255,120), IM_COL32(10,40,120,105),
		IM_COL32(120,230,255,145), IM_COL32(200,250,255,210), IM_COL32(80,210,255,230) }};
	case Hologram:  return { AnimKind::DualScan,    1.55f, 0.09f, 0.80f, {
		IM_COL32(100,230,255,90), IM_COL32(40,80,160,80),
		IM_COL32(180,255,255,130), IM_COL32(255,255,255,190), IM_COL32(120,240,255,220) }};
	case Sunset:    return { AnimKind::ScanY,       0.70f, 0.30f, 0.75f, {
		IM_COL32(255,160,60,125), IM_COL32(200,40,120,105),
		IM_COL32(255,200,140,145), IM_COL32(255,240,200,205), IM_COL32(255,140,80,230) }};
	case Ghost:     return { AnimKind::Sparkle,     1.30f, 0.08f, 0.90f, {
		IM_COL32(240,245,255,95), IM_COL32(140,150,180,65),
		IM_COL32(255,255,255,140), IM_COL32(255,255,255,200), IM_COL32(220,230,255,200) }};
	case Chromatic: return { AnimKind::RainbowFlow, 0.65f, 0.16f, 0.65f, {
		IM_COL32(255,80,180,120), IM_COL32(80,120,255,105),
		IM_COL32(255,255,255,140), IM_COL32(255,255,255,210), IM_COL32(255,255,255,230) }};
	case Plasma:
	default:        return { AnimKind::Diagonal,    1.20f, 0.18f, 0.65f, {
		IM_COL32(110,235,255,125), IM_COL32(70,55,210,100),
		IM_COL32(190,255,255,145), IM_COL32(240,255,255,210), IM_COL32(150,245,255,230) }};
	}
}

void Bounds(const std::vector<ImVec2>& poly, ImVec2& mn, ImVec2& mx)
{
	mn = mx = poly[0];
	for (const auto& p : poly)
	{
		if (p.x < mn.x) mn.x = p.x;
		if (p.y < mn.y) mn.y = p.y;
		if (p.x > mx.x) mx.x = p.x;
		if (p.y > mx.y) mx.y = p.y;
	}
}

template<typename Side, typename Intersect>
std::vector<ImVec2> ClipHalf(const std::vector<ImVec2>& in, Side side, Intersect isect)
{
	std::vector<ImVec2> out;
	int n = (int)in.size();
	if (n < 3)
		return out;

	for (int i = 0; i < n; ++i)
	{
		const ImVec2& cur  = in[i];
		const ImVec2& prev = in[(i + n - 1) % n];
		bool cin = side(cur)  >= 0.0f;
		bool pin = side(prev) >= 0.0f;

		if (cin)
		{
			if (!pin)
				out.push_back(isect(prev, cur));
			out.push_back(cur);
		}

		else if (pin)
		{
			out.push_back(isect(prev, cur));
		}
	}
	return out;
}

std::vector<ImVec2> ClipYBand(const std::vector<ImVec2>& poly, float y0, float y1)
{
	if (y1 < y0)
		std::swap(y0, y1);

	auto lo = ClipHalf(poly,
		[y0](const ImVec2& p) { return p.y - y0; },
		[y0](const ImVec2& a, const ImVec2& b)
		{
			float t = (y0 - a.y) / (b.y - a.y + 1e-6f);
			return ImVec2(a.x + (b.x - a.x) * t, y0);
		});
	return ClipHalf(lo,
		[y1](const ImVec2& p) { return y1 - p.y; },
		[y1](const ImVec2& a, const ImVec2& b)
		{
			float t = (y1 - a.y) / (b.y - a.y + 1e-6f);
			return ImVec2(a.x + (b.x - a.x) * t, y1);
		});
}

std::vector<ImVec2> ClipXBand(const std::vector<ImVec2>& poly, float x0, float x1)
{
	if (x1 < x0)
		std::swap(x0, x1);

	auto lo = ClipHalf(poly,
		[x0](const ImVec2& p) { return p.x - x0; },
		[x0](const ImVec2& a, const ImVec2& b)
		{
			float t = (x0 - a.x) / (b.x - a.x + 1e-6f);
			return ImVec2(x0, a.y + (b.y - a.y) * t);
		});
	return ClipHalf(lo,
		[x1](const ImVec2& p) { return x1 - p.x; },
		[x1](const ImVec2& a, const ImVec2& b)
		{
			float t = (x1 - a.x) / (b.x - a.x + 1e-6f);
			return ImVec2(x1, a.y + (b.y - a.y) * t);
		});
}

std::vector<ImVec2> ClipDiagBand(const std::vector<ImVec2>& poly, float d0, float d1)
{
	if (d1 < d0)
		std::swap(d0, d1);

	auto lo = ClipHalf(poly,
		[d0](const ImVec2& p) { return (p.x + p.y) - d0; },
		[d0](const ImVec2& a, const ImVec2& b)
		{
			float da = a.x + a.y;
			float db = b.x + b.y;
			float t = (d0 - da) / (db - da + 1e-6f);
			return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
		});
	return ClipHalf(lo,
		[d1](const ImVec2& p) { return d1 - (p.x + p.y); },
		[d1](const ImVec2& a, const ImVec2& b)
		{
			float da = a.x + a.y;
			float db = b.x + b.y;
			float t = (d1 - da) / (db - da + 1e-6f);
			return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
		});
}

void FillGradient(ImDrawList* dl, const std::vector<ImVec2>& poly,
                  ImU32 top, ImU32 bot, bool horizontal) {
    if (poly.size() < 3) return;
    ImVec2 mn, mx;
    Bounds(poly, mn, mx);
    const int vtx0 = dl->VtxBuffer.Size;
    dl->AddConvexPolyFilled(poly.data(), (int)poly.size(), top);
    const int vtx1 = dl->VtxBuffer.Size;
    if (vtx1 > vtx0) {
        const ImVec2 p0(mn.x, mn.y);
        const ImVec2 p1 = horizontal ? ImVec2(mx.x, mn.y) : ImVec2(mn.x, mx.y);
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, vtx0, vtx1, p0, p1, top, bot);
    }
}

void FillAll(ImDrawList* dl, const std::vector<std::vector<ImVec2>>& pieces,
             ImU32 top, ImU32 bot, bool horizontal) {
    for (const auto& piece : pieces) {
        if (piece.size() < 3) continue;
        FillGradient(dl, piece, top, bot, horizontal);
    }
}

void PaintY(ImDrawList* dl, const std::vector<std::vector<ImVec2>>& pieces,
            float y0, float y1, ImU32 col) {
    for (const auto& piece : pieces) {
        if (piece.size() < 3) continue;
        auto s = ClipYBand(piece, y0, y1);
        if (s.size() >= 3)
            dl->AddConvexPolyFilled(s.data(), (int)s.size(), col);
    }
}

void PaintX(ImDrawList* dl, const std::vector<std::vector<ImVec2>>& pieces,
            float x0, float x1, ImU32 col) {
    for (const auto& piece : pieces) {
        if (piece.size() < 3) continue;
        auto s = ClipXBand(piece, x0, x1);
        if (s.size() >= 3)
            dl->AddConvexPolyFilled(s.data(), (int)s.size(), col);
    }
}

void PaintDiag(ImDrawList* dl, const std::vector<std::vector<ImVec2>>& pieces,
               float d0, float d1, ImU32 col) {
    for (const auto& piece : pieces) {
        if (piece.size() < 3) continue;
        auto s = ClipDiagBand(piece, d0, d1);
        if (s.size() >= 3)
            dl->AddConvexPolyFilled(s.data(), (int)s.size(), col);
    }
}

void PaintXYCell(ImDrawList* dl, const std::vector<std::vector<ImVec2>>& pieces,
                 float x0, float x1, float y0, float y1, ImU32 col) {
    for (const auto& piece : pieces) {
        if (piece.size() < 3) continue;
        auto s = ClipYBand(ClipXBand(piece, x0, x1), y0, y1);
        if (s.size() >= 3)
            dl->AddConvexPolyFilled(s.data(), (int)s.size(), col);
    }
}

// сами полоски/пульсы
void RunAnim(ImDrawList* dl,
             const std::vector<std::vector<ImVec2>>& pieces,
             const ImVec2& mn, const ImVec2& mx,
             float time, const StyleDef& def, const Palette& pal, float pulse_a)
{
	float w = mx.x - mn.x;
	float h = mx.y - mn.y;
	if (w < 1.f) w = 1.f;
	if (h < 1.f) h = 1.f;

	float phase = std::fmod(time * def.speed, 1.0f);
	ImU32 band = MulAlpha(pal.band, pulse_a);
	ImU32 core = MulAlpha(pal.band_core, pulse_a);

	float bf = def.band_frac;
	if (bf < 0.03f) bf = 0.03f;
	if (bf > 0.5f) bf = 0.5f;

    switch (def.anim) {
    case AnimKind::ScanY: {
        const float bh = h * bf;
        const float cy = mn.y + (h + bh) * phase - bh * 0.5f;
        PaintY(dl, pieces, cy - bh * 0.5f, cy + bh * 0.5f, band);
        PaintY(dl, pieces, cy - bh * 0.14f, cy + bh * 0.14f, core);
        break;
    }
    case AnimKind::ScanX: {

        const float bw = w * bf;
        const float cx = mn.x + (w + bw) * phase - bw * 0.5f;
        PaintX(dl, pieces, cx - bw * 0.5f, cx + bw * 0.5f, band);
        const float p2 = std::fmod(phase + 0.55f, 1.0f);
        const float cx2 = mn.x + (w + bw * 0.6f) * p2 - bw * 0.3f;
        PaintX(dl, pieces, cx2 - bw * 0.25f, cx2 + bw * 0.25f, MulAlpha(core, 0.7f));
        break;
    }
    case AnimKind::Cross: {
        const float bh = h * bf, bw = w * bf;
        const float cy = mn.y + (h + bh) * phase - bh * 0.5f;
        const float cx = mn.x + (w + bw) * std::fmod(phase * 1.35f, 1.0f) - bw * 0.5f;
        PaintY(dl, pieces, cy - bh * 0.5f, cy + bh * 0.5f, band);
        PaintX(dl, pieces, cx - bw * 0.5f, cx + bw * 0.5f, MulAlpha(band, 0.85f));
        PaintY(dl, pieces, cy - bh * 0.12f, cy + bh * 0.12f, core);
        PaintX(dl, pieces, cx - bw * 0.12f, cx + bw * 0.12f, core);
        break;
    }
    case AnimKind::DualScan: {
        const float bh = h * bf;
        const float c0 = mn.y + (h + bh) * phase - bh * 0.5f;
        const float c1 = mn.y + (h + bh) * std::fmod(phase + 0.5f, 1.0f) - bh * 0.5f;
        PaintY(dl, pieces, c0 - bh * 0.5f, c0 + bh * 0.5f, band);
        PaintY(dl, pieces, c1 - bh * 0.5f, c1 + bh * 0.5f, MulAlpha(band, 0.75f));
        PaintY(dl, pieces, c0 - bh * 0.15f, c0 + bh * 0.15f, core);

        if (Hash11(std::floor(time * 28.0f)) > 0.82f)
            PaintY(dl, pieces, mn.y, mx.y, MulAlpha(core, 0.12f));
        break;
    }
    case AnimKind::Pulse: {

        const float t = 0.5f + 0.5f * std::sin(time * def.speed * 6.2831853f);
        const float r = (0.15f + 0.85f * t) * (std::max)(w, h) * 0.55f;
        const float cy = (mn.y + mx.y) * 0.5f;
        PaintY(dl, pieces, cy - r, cy + r, MulAlpha(band, 0.35f + 0.35f * t));
        PaintY(dl, pieces, cy - r * 0.35f, cy + r * 0.35f, MulAlpha(core, 0.45f));
        break;
    }
    case AnimKind::Flicker: {
        const float gate = Hash11(std::floor(time * 18.0f * def.speed));
        if (gate > 0.35f) {
            const float a = 0.25f + gate * 0.75f;
            PaintY(dl, pieces, mn.y, mx.y, MulAlpha(band, a * 0.35f));
        }

        if (Hash11(std::floor(time * 11.0f) + 3.7f) > 0.72f) {
            const float yh = mn.y + h * Hash11(std::floor(time * 9.0f));
            PaintY(dl, pieces, yh, yh + h * 0.08f, core);
        }
        break;
    }
    case AnimKind::Stripes: {
        const float step = (std::max)(3.0f, h * bf);
        const float off = std::fmod(time * def.speed * h * 0.55f, step * 2.0f);
        for (float y = mn.y - step * 2.0f + off; y < mx.y + step; y += step * 2.0f)
            PaintY(dl, pieces, y, y + step * 0.55f, band);

        const float tick = mn.y + std::fmod(time * def.speed * h, h);
        PaintY(dl, pieces, tick, tick + step * 0.35f, core);
        break;
    }
    case AnimKind::Rain: {
        int cols = 7;
        float cw = w / (float)cols;
        for (int i = 0; i < cols; ++i) {
            const float seed = (float)i * 17.13f;
            const float p = std::fmod(phase + Hash11(seed), 1.0f);
            const float drop_h = h * (0.12f + 0.18f * Hash11(seed + 1.0f));
            const float cy = mn.y + (h + drop_h) * p - drop_h * 0.5f;
            const float x0 = mn.x + cw * (float)i + cw * 0.15f;
            const float x1 = x0 + cw * 0.55f;
            for (const auto& piece : pieces) {
                if (piece.size() < 3) continue;
                auto s = ClipYBand(ClipXBand(piece, x0, x1), cy - drop_h * 0.5f, cy + drop_h * 0.5f);
                if (s.size() >= 3)
                    dl->AddConvexPolyFilled(s.data(), (int)s.size(), band);
            }
        }
        break;
    }
    case AnimKind::Wave: {
        int segs = 8;
        float seg_w = w / (float)segs;
        float bh = h * bf;
        for (int i = 0; i < segs; ++i) {
            const float u = (float)i / (float)segs;
            const float wave = 0.5f + 0.5f * std::sin((u * 4.0f + time * def.speed) * 6.2831853f);
            const float cy = mn.y + h * (0.25f + 0.5f * wave);
            const float x0 = mn.x + seg_w * (float)i;
            const float x1 = x0 + seg_w + 1.0f;
            for (const auto& piece : pieces) {
                if (piece.size() < 3) continue;
                auto s = ClipYBand(ClipXBand(piece, x0, x1), cy - bh * 0.5f, cy + bh * 0.5f);
                if (s.size() >= 3)
                    dl->AddConvexPolyFilled(s.data(), (int)s.size(), band);
            }
        }
        break;
    }
    case AnimKind::Rise: {

        for (int i = 0; i < 4; ++i) {
            const float sp = 0.55f + 0.35f * (float)i;
            const float p = std::fmod(time * def.speed * sp + (float)i * 0.21f, 1.0f);
            const float bh = h * (bf * (0.7f + 0.2f * (float)i));

            const float cy = mx.y - (h + bh) * p + bh * 0.5f;
            const float a = 0.45f + 0.15f * (float)(3 - i);
            PaintY(dl, pieces, cy - bh * 0.5f, cy + bh * 0.5f, MulAlpha(band, a));
        }

        if (Hash11(std::floor(time * 22.0f)) > 0.55f)
            PaintY(dl, pieces, mn.y, mn.y + h * 0.12f, MulAlpha(core, 0.55f));
        break;
    }
    case AnimKind::Ripple: {
        const float cx = (mn.x + mx.x) * 0.5f;
        const float cy = (mn.y + mx.y) * 0.5f;
        const float max_r = 0.5f * std::sqrt(w * w + h * h);
        for (int ring = 0; ring < 3; ++ring) {
            const float p = std::fmod(phase + (float)ring * 0.33f, 1.0f);
            const float r = p * max_r;
            const float th = (std::max)(2.5f, max_r * bf);

            PaintY(dl, pieces, cy - r - th, cy - r + th, MulAlpha(band, 0.55f));
            PaintY(dl, pieces, cy + r - th, cy + r + th, MulAlpha(band, 0.55f));
            PaintX(dl, pieces, cx - r - th, cx - r + th, MulAlpha(band, 0.40f));
            PaintX(dl, pieces, cx + r - th, cx + r + th, MulAlpha(band, 0.40f));
        }
        PaintY(dl, pieces, cy - h * 0.04f, cy + h * 0.04f, MulAlpha(core, 0.5f));
        break;
    }
    case AnimKind::Glitch: {
        for (int i = 0; i < 5; ++i) {
            const float seed = std::floor(time * def.speed * 6.0f) + (float)i * 13.7f;
            if (Hash11(seed) < 0.45f) continue;
            const float yh = mn.y + h * Hash11(seed + 1.1f);
            const float hh = h * (0.03f + 0.10f * Hash11(seed + 2.2f));
            const float xshift = (Hash11(seed + 3.3f) - 0.5f) * w * 0.25f;

            PaintY(dl, pieces, yh, yh + hh, MulAlpha(band, 0.55f + 0.4f * Hash11(seed)));
            PaintX(dl, pieces, mn.x + xshift, mn.x + xshift + w * 0.35f, MulAlpha(core, 0.25f));
        }
        break;
    }
    case AnimKind::Sparkle: {
        int cells = 12;
        float cw = w / 4.0f;
        float ch = h / 5.0f;
        for (int i = 0; i < cells; ++i) {
            const float seed = (float)i * 9.17f + std::floor(time * def.speed * 5.0f);
            if (Hash11(seed) < 0.55f) continue;
            const float ux = Hash11(seed + 0.3f);
            const float uy = Hash11(seed + 0.7f);
            const float x0 = mn.x + ux * (w - cw);
            const float y0 = mn.y + uy * (h - ch);
            const float a = 0.35f + 0.65f * Hash11(seed + 1.4f);
            PaintXYCell(dl, pieces, x0, x0 + cw * 0.55f, y0, y0 + ch * 0.45f, MulAlpha(core, a));
        }

        const float breath = 0.5f + 0.5f * std::sin(time * def.speed * 3.5f);
        PaintY(dl, pieces, mn.y, mx.y, MulAlpha(band, 0.08f + 0.10f * breath));
        break;
    }
    case AnimKind::Diagonal:
    case AnimKind::RainbowFlow: {
        const float dmin = mn.x + mn.y;
        const float dmax = mx.x + mx.y;
        const float span = (std::max)(1.0f, dmax - dmin);
        const float bd = span * bf;
        const float cd = dmin + (span + bd) * phase - bd * 0.5f;
        PaintDiag(dl, pieces, cd - bd * 0.5f, cd + bd * 0.5f, band);
        PaintDiag(dl, pieces, cd - bd * 0.15f, cd + bd * 0.15f, core);
        if (def.anim == AnimKind::RainbowFlow) {

            const float p2 = std::fmod(phase + 0.4f, 1.0f);
            const float cy = mn.y + h * p2;
            PaintY(dl, pieces, cy - h * 0.04f, cy + h * 0.04f, MulAlpha(core, 0.55f));
        }
        break;
    }
    default: break;
    }
}

Palette PaletteFromOverride(const float* c)
{
	int r = (int)(c[0] * 255.f);
	int g = (int)(c[1] * 255.f);
	int b = (int)(c[2] * 255.f);
	float a = c[3];
	if (a < 0.f) a = 0.f;
	if (a > 1.f) a = 1.f;

	int rd = (int)(r * 0.40f);
	int gd = (int)(g * 0.40f);
	int bd = (int)(b * 0.40f);
	int rh = r + 50;
	int gh = g + 50;
	int bh = b + 50;
	if (rh > 255) rh = 255;
	if (gh > 255) gh = 255;
	if (bh > 255) bh = 255;

	return {
		IM_COL32(r, g, b, (int)(a * 140.f)),
		IM_COL32(rd, gd, bd, (int)(a * 105.f)),
		IM_COL32(rh, gh, bh, (int)(a * 165.f)),
		IM_COL32(rh, gh, bh, (int)(a * 220.f)),
		IM_COL32(r, g, b, (int)(a * 235.f)),
	};
}

}

const char* const* StyleNames() { return k_names; }

ImU32 OutlineColor(int style, bool aim_highlight, const float* color_override)
{
	if (color_override)
		return PaletteFromOverride(color_override).outline;

	if (aim_highlight)
		return AimPalette().outline;

	if (style < 0) style = 0;
	if (style > StyleCount - 1) style = StyleCount - 1;

	if (style == Chromatic)
		return HSVA(std::fmod((float)ImGui::GetTime() * 0.35f, 1.0f), 0.85f, 1.0f, 0.90f);

	return DefFor(style).pal.outline;
}

void Triangulate(const std::vector<ImVec2>& poly,
                 std::vector<std::vector<ImVec2>>& out_tris)
{
	out_tris.clear();
	int n0 = (int)poly.size();
	if (n0 < 3)
		return;

	if (n0 == 3)
	{
		out_tris.push_back(poly);
		return;
	}

	auto area2 = [](const ImVec2& a, const ImVec2& b, const ImVec2& c)
	{
		return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
	};
	auto point_in_tri = [&](const ImVec2& p, const ImVec2& a,
	                        const ImVec2& b, const ImVec2& c)
	{
		float a0 = area2(a, b, p);
		float a1 = area2(b, c, p);
		float a2 = area2(c, a, p);
		bool has_neg = (a0 < 0) || (a1 < 0) || (a2 < 0);
		bool has_pos = (a0 > 0) || (a1 > 0) || (a2 > 0);
		return !(has_neg && has_pos);
	};

	std::vector<int> idx((size_t)n0);
	for (int i = 0; i < n0; ++i)
		idx[i] = i;

	float signed_area = 0.0f;
	for (int i = 0; i < n0; ++i)
	{
		const ImVec2& a = poly[i];
		const ImVec2& b = poly[(i + 1) % n0];
		signed_area += a.x * b.y - b.x * a.y;
	}
	if (signed_area < 0.0f)
		std::reverse(idx.begin(), idx.end());

	out_tris.reserve((size_t)(n0 - 2));
	int guard = n0 * n0;
	while ((int)idx.size() > 3 && guard-- > 0)
	{
		int m = (int)idx.size();
		bool clipped = false;
		for (int i = 0; i < m; ++i)
		{
			int i0 = idx[(i + m - 1) % m];
			int i1 = idx[i];
			int i2 = idx[(i + 1) % m];
			const ImVec2& a = poly[i0];
			const ImVec2& b = poly[i1];
			const ImVec2& c = poly[i2];
			if (area2(a, b, c) <= 0.0f)
				continue;

			bool ear = true;
			for (int j = 0; j < m; ++j)
			{
				int ij = idx[j];
				if (ij == i0 || ij == i1 || ij == i2)
					continue;
				if (point_in_tri(poly[ij], a, b, c))
				{
					ear = false;
					break;
				}
			}
			if (!ear)
				continue;

			out_tris.push_back({ a, b, c });
			idx.erase(idx.begin() + i);
			clipped = true;
			break;
		}
		if (!clipped)
			break;
	}
	if (idx.size() == 3)
		out_tris.push_back({ poly[idx[0]], poly[idx[1]], poly[idx[2]] });
}

// филл полигонов шейдер-чамсами
void DrawFill(ImDrawList* draw_list,
              const std::vector<std::vector<ImVec2>>& pieces,
              float time,
              int style,
              bool aim_highlight,
              bool lite,
              const float* color_override)
{
	if (!draw_list || pieces.empty())
		return;

	if (style < 0) style = 0;
	if (style > StyleCount - 1) style = StyleCount - 1;

	bool have_bounds = false;
	ImVec2 body_mn{}, body_mx{};
	for (const auto& p : pieces)
	{
		if (p.size() < 3)
			continue;
		ImVec2 mn, mx;
		Bounds(p, mn, mx);
		if (!have_bounds)
		{
			body_mn = mn;
			body_mx = mx;
			have_bounds = true;
		}

		else
		{
			if (mn.x < body_mn.x) body_mn.x = mn.x;
			if (mn.y < body_mn.y) body_mn.y = mn.y;
			if (mx.x > body_mx.x) body_mx.x = mx.x;
			if (mx.y > body_mx.y) body_mx.y = mx.y;
		}
	}
	if (!have_bounds)
		return;

	StyleDef def = DefFor(style);
	(void)lite;

	Palette pal = def.pal;
	if (color_override)
		pal = PaletteFromOverride(color_override);

	else if (aim_highlight)
		pal = AimPalette();

	if (!color_override && !aim_highlight && style == Chromatic)
	{
		float h = std::fmod(time * def.speed * 0.35f, 1.0f);
		pal.top = HSVA(h, 0.85f, 1.0f, 0.48f);
		pal.bot = HSVA(h + 0.33f, 0.90f, 0.75f, 0.40f);
		pal.band = HSVA(h + 0.12f, 0.55f, 1.0f, 0.55f);
		pal.band_core = HSVA(h + 0.05f, 0.25f, 1.0f, 0.80f);
	}
	if (!color_override && !aim_highlight && style == Aurora)
	{
		float w = 0.5f + 0.5f * std::sin(time * 0.7f);
		pal.top = LerpCol(pal.top, IM_COL32(255, 120, 200, 120), w * 0.55f);
		pal.bot = LerpCol(pal.bot, IM_COL32(80, 200, 255, 105), (1.0f - w) * 0.55f);
	}
	if (!color_override && !aim_highlight && style == Gold)
	{
		float w = 0.5f + 0.5f * std::sin(time * def.speed * 2.0f);
		pal.top = LerpCol(pal.top, IM_COL32(255, 255, 200, 150), w * 0.4f);
		pal.bot = LerpCol(pal.bot, IM_COL32(180, 100, 20, 110), (1.0f - w) * 0.35f);
	}
	if (!color_override && !aim_highlight && style == Sunset)
	{
		float w = 0.5f + 0.5f * std::sin(time * 0.45f);
		pal.top = LerpCol(pal.top, IM_COL32(255, 80, 100, 125), w * 0.5f);
		pal.bot = LerpCol(pal.bot, IM_COL32(80, 20, 120, 105), (1.0f - w) * 0.45f);
	}

	float pulse_a = 1.0f;
	if (def.anim == AnimKind::Pulse || def.anim == AnimKind::Sparkle)
		pulse_a = 0.55f + 0.45f * (0.5f + 0.5f * std::sin(time * def.speed * 6.2831853f));

	else if (def.anim == AnimKind::Flicker)
		pulse_a = 0.40f + 0.60f * Hash11(std::floor(time * 16.0f));

	float fresnel = def.fresnel;
	if (fresnel < 0.f) fresnel = 0.f;
	if (fresnel > 1.f) fresnel = 1.f;

	ImU32 top = MulAlpha(pal.top, pulse_a * (0.55f + 0.45f * fresnel));
	ImU32 bot = MulAlpha(pal.bot, pulse_a);

    ImDrawListFlags backup = draw_list->Flags;
    draw_list->Flags &= ~ImDrawListFlags_AntiAliasedFill;

    const bool horiz = (def.anim == AnimKind::ScanX || def.anim == AnimKind::Wave);
    FillAll(draw_list, pieces, top, bot, horiz);
    RunAnim(draw_list, pieces, body_mn, body_mx, time, def, pal, pulse_a);

    draw_list->Flags = backup;
}

namespace {

std::vector<ImVec2> ConvexHull(std::vector<ImVec2> pts)
{
    if (pts.size() < 3)
        return pts;

    std::sort(pts.begin(), pts.end(), [](const ImVec2& a, const ImVec2& b) {
        if (a.x < b.x) return true;
        if (a.x > b.x) return false;
        return a.y < b.y;
    });

    auto cross = [](const ImVec2& o, const ImVec2& a, const ImVec2& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };

    std::vector<ImVec2> hull(pts.size() * 2);
    int k = 0;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0.0f)
            k--;
        hull[k++] = pts[i];
    }

    for (int i = (int)pts.size() - 2, t = k + 1; i >= 0; --i)
    {
        while (k >= t && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0.0f)
            k--;
        hull[k++] = pts[i];
    }

    hull.resize(k > 0 ? (std::size_t)(k - 1) : 0);
    return hull;
}

std::vector<ImVec2> ClipHalfPlane(const std::vector<ImVec2>& poly,
                                  const ImVec2& p, const ImVec2& q, float s)
{
    std::vector<ImVec2> out;
    int n = (int)poly.size();
    if (n < 3)
        return out;
    out.reserve((std::size_t)n + 2);

    auto f = [&](const ImVec2& v) {
        return s * ((q.x - p.x) * (v.y - p.y) - (q.y - p.y) * (v.x - p.x));
    };

    for (int i = 0; i < n; ++i)
    {
        const ImVec2& a = poly[i];
        const ImVec2& b = poly[(i + 1) % n];
        float fa = f(a), fb = f(b);
        if (fa >= 0.0f)
            out.push_back(a);

        if ((fa < 0.0f) != (fb < 0.0f))
        {
            float t = fa / (fa - fb);
            out.push_back(ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t));
        }
    }

    if (out.size() < 3)
        out.clear();
    return out;
}

void SubtractPoly(std::vector<ImVec2> piece, const std::vector<ImVec2>& B,
                  std::vector<std::vector<ImVec2>>& out)
{
    int n = (int)B.size();
    if (n < 3)
    {
        if (piece.size() >= 3)
            out.push_back(std::move(piece));
        return;
    }

    ImVec2 c(0, 0);
    for (const auto& v : B)
    {
        c.x += v.x;
        c.y += v.y;
    }
    c.x /= n;
    c.y /= n;

    for (int i = 0; i < n && piece.size() >= 3; ++i)
    {
        const ImVec2& p = B[i];
        const ImVec2& q = B[(i + 1) % n];
        float cs = (q.x - p.x) * (c.y - p.y) - (q.y - p.y) * (c.x - p.x);
        float s = -1.0f;
        if (cs >= 0.0f)
            s = 1.0f;

        auto outside = ClipHalfPlane(piece, p, q, -s);
        if (!outside.empty())
            out.push_back(std::move(outside));
        piece = ClipHalfPlane(piece, p, q, s);
    }
}

bool SegInsidePoly(const ImVec2& a, const ImVec2& b,
                   const std::vector<ImVec2>& poly,
                   float& out_t0, float& out_t1)
{
    int n = (int)poly.size();
    if (n < 3)
        return false;

    ImVec2 c(0, 0);
    for (const auto& p : poly)
    {
        c.x += p.x;
        c.y += p.y;
    }
    c.x /= n;
    c.y /= n;

    float t0 = 0.0f, t1 = 1.0f;
    float eps = 0.25f;

    for (int i = 0; i < n; ++i)
    {
        const ImVec2& p = poly[i];
        const ImVec2& q = poly[(i + 1) % n];
        float ex = q.x - p.x, ey = q.y - p.y;

        auto side = [&](const ImVec2& v) { return ex * (v.y - p.y) - ey * (v.x - p.x); };
        float s = -1.0f;
        if (side(c) >= 0.0f)
            s = 1.0f;

        float f0 = s * side(a);
        float f1 = s * side(b);
        float df = f1 - f0;

        if (std::fabs(df) < 1e-6f)
        {
            if (f0 < -eps)
                return false;
            continue;
        }

        float tc = (-eps - f0) / df;
        if (df > 0.0f)
        {
            if (tc > t0)
                t0 = tc;
        }
        else
        {
            if (tc < t1)
                t1 = tc;
        }

        if (t0 >= t1)
            return false;
    }

    if (t0 < 0.0f) t0 = 0.0f;
    if (t1 > 1.0f) t1 = 1.0f;
    out_t0 = t0;
    out_t1 = t1;
    return out_t1 > out_t0;
}

void DrawSegmentOutsideUnion(ImDrawList* dl, const ImVec2& a, const ImVec2& b,
                             const std::vector<std::vector<ImVec2>>& polys,
                             int skip, ImU32 color)
{
    std::vector<std::pair<float, float>> covered;
    for (int i = 0; i < (int)polys.size(); ++i)
    {
        if (i == skip)
            continue;
        float t0, t1;
        if (SegInsidePoly(a, b, polys[i], t0, t1))
            covered.emplace_back(t0, t1);
    }

    auto lerp_pt = [&](float t) { return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); };

    if (covered.empty())
    {
        dl->AddLine(a, b, color, 1.0f);
        return;
    }

    std::sort(covered.begin(), covered.end());

    float min_piece = 0.002f;
    float cursor = 0.0f;
    for (const auto& iv : covered)
    {
        if (iv.first > cursor + min_piece)
            dl->AddLine(lerp_pt(cursor), lerp_pt(iv.first), color, 1.0f);
        if (iv.second > cursor)
            cursor = iv.second;
        if (cursor >= 1.0f)
            break;
    }

    if (cursor < 1.0f - min_piece)
        dl->AddLine(lerp_pt(cursor), lerp_pt(1.0f), color, 1.0f);
}

}

void DrawPlayer(ImDrawList* draw_list,
                std::uint64_t character,
                const Mesh::Matrix4x4& view,
                const Mesh::Vector2& viewport,
                float scale_x,
                float scale_y,
                float time,
                int style,
                bool aim_highlight,
                const float* color_override)
{
    if (!draw_list || !g_Memory.IsValid(character) || style < 0 || style >= StyleCount)
        return;

    struct PartCorners { ImVec2 pt[8]; bool full; };
    std::vector<PartCorners> chams_parts;

    auto w2s = [&](const Mesh::Vector3& p, ImVec2& out) -> bool {
        float w = p.x * view.m[3][0] + p.y * view.m[3][1] + p.z * view.m[3][2] + view.m[3][3];
        if (w <= 0.0f)
            return false;
        float x = p.x * view.m[0][0] + p.y * view.m[0][1] + p.z * view.m[0][2] + view.m[0][3];
        float y = p.x * view.m[1][0] + p.y * view.m[1][1] + p.z * view.m[1][2] + view.m[1][3];
        float inv = 1.f / w;
        out.x = ((viewport.x * 0.5f) + (x * viewport.x * 0.5f) * inv) * scale_x;
        out.y = ((viewport.y * 0.5f) - (y * viewport.y * 0.5f) * inv) * scale_y;
        return true;
    };

    for (const auto& e : MeshParser::CollectDrawable(character))
    {
        if (!e.part)
            continue;
        BasePart bp(e.part);
        Mesh::Vector3 pos, sz;
        Mesh::Matrix4x4 rot;
        if (!bp.GetFrameData(pos, rot, sz))
            continue;
        if (sz.x < 0.01f && sz.y < 0.01f && sz.z < 0.01f)
            continue;

        const Mesh::Vector3 half{ sz.x * 0.5f, sz.y * 0.5f, sz.z * 0.5f };
        const Mesh::Vector3 local[8] = {
            { -half.x, -half.y, -half.z }, { -half.x, -half.y,  half.z },
            { -half.x,  half.y, -half.z }, { -half.x,  half.y,  half.z },
            {  half.x, -half.y, -half.z }, {  half.x, -half.y,  half.z },
            {  half.x,  half.y, -half.z }, {  half.x,  half.y,  half.z }
        };
        PartCorners pc{};
        pc.full = true;
        for (int i = 0; i < 8; ++i)
        {
            const Mesh::Vector3 wr{
                pos.x + rot.m[0][0] * local[i].x + rot.m[0][1] * local[i].y + rot.m[0][2] * local[i].z,
                pos.y + rot.m[1][0] * local[i].x + rot.m[1][1] * local[i].y + rot.m[1][2] * local[i].z,
                pos.z + rot.m[2][0] * local[i].x + rot.m[2][1] * local[i].y + rot.m[2][2] * local[i].z
            };
            if (!w2s(wr, pc.pt[i]))
                pc.full = false;
        }
        if (pc.full)
            chams_parts.push_back(pc);
    }

    if (chams_parts.empty())
        return;

    std::vector<std::vector<ImVec2>> hulls;
    hulls.reserve(chams_parts.size());
    for (const auto& pc : chams_parts)
    {
        std::vector<ImVec2> pts(pc.pt, pc.pt + 8);
        hulls.push_back(ConvexHull(std::move(pts)));
    }

    std::vector<std::vector<ImVec2>> clipped;
    clipped.reserve(hulls.size() * 2);
    for (int i = 0; i < (int)hulls.size(); ++i)
    {
        if (hulls[i].size() < 3)
            continue;
        std::vector<std::vector<ImVec2>> pieces{ hulls[i] };
        for (int j = 0; j < i && !pieces.empty(); ++j)
        {
            if (hulls[j].size() < 3)
                continue;
            std::vector<std::vector<ImVec2>> next;
            for (auto& piece : pieces)
                SubtractPoly(std::move(piece), hulls[j], next);
            pieces = std::move(next);
        }
        for (auto& piece : pieces)
            if (piece.size() >= 3)
                clipped.push_back(std::move(piece));
    }

    if (clipped.empty())
        return;

    DrawFill(draw_list, clipped, time, style, aim_highlight, false, color_override);

    const ImU32 outline_col = color_override
        ? OutlineColor(style, false, color_override)
        : (aim_highlight ? OutlineColor(style, true) : OutlineColor(style, false));
    for (int i = 0; i < (int)hulls.size(); ++i)
    {
        const auto& hull = hulls[i];
        const int n = (int)hull.size();
        if (n < 2)
            continue;
        for (int e = 0; e < n; ++e)
            DrawSegmentOutsideUnion(draw_list, hull[e], hull[(e + 1) % n], hulls, i, outline_col);
    }
}

}
}
}

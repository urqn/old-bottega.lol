#include "render.h"
#include "shared.h"
#include "inmem.h"
#include "offsets_native.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nchams {
namespace {

ncfg::Config* s_cfg = nullptr;

uintptr_t s_base = 0;
uintptr_t s_dm   = 0;
uintptr_t s_ve   = 0;

// --- D3D state (only touched from the Present hook thread) ---
ID3D11Device*           s_dev   = nullptr;
ID3D11DeviceContext*    s_ctx   = nullptr;
ID3D11RenderTargetView* s_rtv   = nullptr;
ID3D11VertexShader*     s_vs    = nullptr;
ID3D11PixelShader*      s_ps    = nullptr;
ID3D11InputLayout*      s_il    = nullptr;
ID3D11Buffer*           s_vb    = nullptr;
ID3D11Buffer*           s_cb    = nullptr;
ID3D11RasterizerState*  s_rs    = nullptr;
ID3D11BlendState*       s_bs    = nullptr;
bool s_render_ok = false;

struct Vtx { float x, y; float r, g, b, a; }; // 24 bytes
struct CbData { float vp[2]; float vp_pad[2]; };

constexpr size_t kMaxVerts = 120000;

bool g_is_local = false;

inline bool SafeF(float v) { return std::isfinite(v); }

// Walk filters mirroring MeshParser (external).
bool IsSkipClass(const std::string& cls) {
    return cls == "Humanoid" || cls == "Script" || cls == "LocalScript" ||
           cls == "ModuleScript" || cls == "Sound" || cls == "Animation" ||
           cls == "Animator" || cls == "BindableEvent" || cls == "BindableFunction" ||
           cls == "RemoteEvent" || cls == "RemoteFunction" || cls == "Attachment" ||
           cls == "Motor6D" || cls == "Weld" || cls == "WeldConstraint" ||
           cls == "ManualWeld" || cls == "Snap" || cls == "BodyColors" ||
           cls == "Shirt" || cls == "Pants" || cls == "ShirtGraphic" ||
           cls == "BodyGyro" || cls == "BodyVelocity" || cls == "BodyForce" ||
           cls == "Highlight" || cls == "BillboardGui" || cls == "SurfaceGui" ||
           cls == "ProximityPrompt" || cls == "ClickDetector" ||
           cls == "WrapTarget" || cls == "WrapLayer" || cls == "SurfaceAppearance" ||
           cls == "NoCollisionConstraint";
}

bool NameHas(const std::string& s, const char* needle) {
    if (s.empty() || !needle) return false;
    std::string a = s;
    std::string b = needle;
    for (char& c : a) c = (char)std::tolower((unsigned char)c);
    for (char& c : b) c = (char)std::tolower((unsigned char)c);
    return a.find(b) != std::string::npos;
}

bool IsSkipPartName(const std::string& name) {
    if (name.empty()) return false;
    if (name == "HumanoidRootPart" || name == "CollisionCapsule") return true;
    return NameHas(name, "collision") || NameHas(name, "hitbox") ||
           NameHas(name, "capsule") || NameHas(name, "nocol");
}

bool IsSkipContainerName(const std::string& name) {
    return NameHas(name, "weapon") || NameHas(name, "gun") ||
           NameHas(name, "viewmodel") || NameHas(name, "firstperson") ||
           NameHas(name, "viewarms") || NameHas(name, "fakearm");
}

bool IsAccessoryClass(const std::string& cls) {
    return cls == "Accessory" || cls == "Hat" || cls == "Accoutrement";
}

bool IsBasePartClass(const std::string& cls) {
    return cls == "Part" || cls == "MeshPart" || cls == "UnionOperation" ||
           cls == "NegateOperation" || cls == "IntersectOperation" ||
           cls == "TrussPart" || cls == "WedgePart" || cls == "CornerWedgePart" ||
           cls == "Seat" || cls == "VehicleSeat" || cls == "SpawnLocation" ||
           cls == "CylinderPart" || cls == "BallPart";
}

void GatherParts(uintptr_t node, int depth, const std::string& container,
                 bool under_acc, std::unordered_set<uintptr_t>& seen,
                 std::vector<uintptr_t>& out) {
    if (depth > 12 || !node) return;
    const std::string cls = inmem::GetClass(node);
    if (cls.empty() || IsSkipClass(cls)) return;
    if (cls == "Tool") return;

    if (IsAccessoryClass(cls)) {
        const std::string acc = inmem::GetName(node);
        for (const uintptr_t c : inmem::Children(node))
            GatherParts(c, depth + 1, acc, true, seen, out);
        return;
    }

    if (!under_acc && (cls == "Model" || cls == "Folder")) {
        const std::string nm = inmem::GetName(node);
        if (IsSkipContainerName(nm)) return;
        const bool maybe_acc = NameHas(nm, "accessory") || NameHas(nm, "hat") ||
                               NameHas(nm, "hair") || NameHas(nm, "layer") ||
                               NameHas(nm, "mesh") || NameHas(nm, "armor") ||
                               NameHas(nm, "clothing") || NameHas(nm, "gear");
        for (const uintptr_t c : inmem::Children(node))
            GatherParts(c, depth + 1, maybe_acc ? nm : container,
                        under_acc || maybe_acc, seen, out);
        return;
    }

    if (cls == "CharacterMesh") return;

    if (IsBasePartClass(cls)) {
        const std::string nm = inmem::GetName(node);
        if (IsSkipPartName(nm)) return;
        if (!inmem::PrimitiveOf(node)) return;
        if (seen.insert(node).second) out.push_back(node);
        const bool acc = under_acc || nm == "Handle";
        for (const uintptr_t c : inmem::Children(node)) {
            const std::string cc = inmem::GetClass(c);
            if (cc.empty()) continue;
            if (IsBasePartClass(cc) || IsAccessoryClass(cc) ||
                cc == "Model" || cc == "Folder")
                GatherParts(c, depth + 1, container, acc, seen, out);
        }
        return;
    }

    for (const uintptr_t c : inmem::Children(node))
        GatherParts(c, depth + 1, container, under_acc, seen, out);
}

struct PartList { uint64_t t; std::vector<uintptr_t> parts; };
std::unordered_map<uintptr_t, PartList> s_part_cache;
uint64_t s_now_ms = 0;

uint64_t NowMs() {
    static const uint64_t qpc = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return (uint64_t)f.QuadPart;
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return (c.QuadPart * 1000ull) / qpc;
}

const std::vector<uintptr_t>& CachedParts(uintptr_t character, bool local) {
    static uintptr_t s_localkey = 0;
    static bool      s_was_local = false;
    if (s_localkey != character || s_was_local != local)
        s_part_cache.clear();
    s_localkey = character;
    s_was_local = local;

    auto& slot = s_part_cache[character];
    if (slot.parts.empty() || s_now_ms - slot.t > 1000) {
        slot.parts.clear();
        std::unordered_set<uintptr_t> seen;
        GatherParts(character, 0, "", false, seen, slot.parts);
        slot.t = s_now_ms;
    }
    return slot.parts;
}

bool Resolve() {
    if (!s_base) s_base = (uintptr_t)GetModuleHandleW(L"RobloxPlayerBeta.exe");
    if (!s_base) return false;
    const uintptr_t fake = inmem::Rd(s_base + nofs::FakeDataModel::Pointer);
    s_dm = fake ? inmem::Rd(fake + nofs::FakeDataModel::RealDataModel) : 0;
    s_ve = inmem::Rd(s_base + nofs::VisualEngine::Pointer);
    return s_dm != 0 && s_ve != 0;
}

bool ReadView(float m[16]) {
    if (!s_ve) return false;
    if (!inmem::TryRead(s_ve + nofs::VisualEngine::ViewMatrix, m, 64)) return false;
    for (int i = 0; i < 16; ++i)
        if (!SafeF(m[i])) return false;
    if (std::fabs(m[15]) < 1e-6f) return false;
    return true;
}

// W2S identical to MeshChams::W2S (row-major 4x4, explicit screen dims).
bool W2S(const float m[16], float dim_x, float dim_y, float px, float py, float pz,
         float& sx, float& sy) {
    const float w = px * m[12] + py * m[13] + pz * m[14] + m[15];
    if (w < 0.01f) return false;
    const float x = px * m[0] + py * m[1] + pz * m[2] + m[3];
    const float y = px * m[4] + py * m[5] + pz * m[6] + m[7];
    const float inv = 1.f / w;
    sx = (dim_x * 0.5f) + (x * inv * dim_x * 0.5f);
    sy = (dim_y * 0.5f) - (y * inv * dim_y * 0.5f);
    return true;
}

// Box corners + 12 triangles identical to MeshChams::DrawBoxFallback.
void PushBox(std::vector<Vtx>& verts, const float m[16], float dim_x, float dim_y,
             float px, float py, float pz, const float rot[9], float szx, float szy,
             float szz, float cr, float cg, float cb, float ca) {
    if (szx < 0.01f && szy < 0.01f && szz < 0.01f) return;
    const float hx = szx * 0.5f, hy = szy * 0.5f, hz = szz * 0.5f;

    float sp[8][2];
    bool  sv[8]{};
    bool  any = false;
    static const float lc[8][3] = {
        { -1.f, -1.f, -1.f }, { -1.f, -1.f,  1.f }, { -1.f,  1.f, -1.f }, { -1.f,  1.f,  1.f },
        {  1.f, -1.f, -1.f }, {  1.f, -1.f,  1.f }, {  1.f,  1.f, -1.f }, {  1.f,  1.f,  1.f },
    };
    for (int i = 0; i < 8; ++i) {
        const float lx = lc[i][0] * hx, ly = lc[i][1] * hy, lz = lc[i][2] * hz;
        const float wx = px + rot[0] * lx + rot[1] * ly + rot[2] * lz;
        const float wy = py + rot[3] * lx + rot[4] * ly + rot[5] * lz;
        const float wz = pz + rot[6] * lx + rot[7] * ly + rot[8] * lz;
        sv[i] = W2S(m, dim_x, dim_y, wx, wy, wz, sp[i][0], sp[i][1]);
        sp[i][0] = std::min(std::max(sp[i][0], -8000.f), 8000.f);
        sp[i][1] = std::min(std::max(sp[i][1], -8000.f), 8000.f);
        any = any || sv[i];
    }
    if (!any) return;
    static const int tris[12][3] = {
        { 0, 1, 3 }, { 0, 3, 2 }, { 4, 6, 7 }, { 4, 7, 5 },
        { 0, 4, 5 }, { 0, 5, 1 }, { 2, 3, 7 }, { 2, 7, 6 },
        { 0, 2, 6 }, { 0, 6, 4 }, { 1, 5, 7 }, { 1, 7, 3 },
    };
    for (const auto& t : tris) {
        if (!sv[t[0]] || !sv[t[1]] || !sv[t[2]]) continue;
        const float ax = sp[t[0]][0], ay = sp[t[0]][1];
        const float bx = sp[t[1]][0], by = sp[t[1]][1];
        const float cx = sp[t[2]][0], cy = sp[t[2]][1];
        const float crs = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (crs < 0.15f) continue;
        verts.push_back({ ax, ay, cr, cg, cb, ca });
        verts.push_back({ bx, by, cr, cg, cb, ca });
        verts.push_back({ cx, cy, cr, cg, cb, ca });
    }
}

void PushBoxFromPart(uintptr_t part, const float m[16], float dim_x, float dim_y,
                     float cr, float cg, float cb, float ca, std::vector<Vtx>& verts) {
    const uintptr_t prim = inmem::PrimitiveOf(part);
    if (!prim) return;
    struct FrameData { float r[9]; float px, py, pz; };
    FrameData fd{};
    if (!inmem::TryRead(prim + nofs::Primitive::Frame, &fd, sizeof(fd))) return;
    if (!(SafeF(fd.px) && SafeF(fd.py) && SafeF(fd.pz))) return;
    float sz[3]{};
    if (!inmem::TryRead(prim + nofs::Primitive::Size, sz, sizeof(sz))) return;
    for (int i = 0; i < 3; ++i)
        if (!SafeF(sz[i])) return;
    constexpr float kMaxExt = 60.f;
    if (sz[0] > kMaxExt || sz[1] > kMaxExt || sz[2] > kMaxExt) return;
    if (sz[0] < 0.001f && sz[1] < 0.001f && sz[2] < 0.001f) return;
    PushBox(verts, m, dim_x, dim_y, fd.px, fd.py, fd.pz, fd.r, sz[0], sz[1], sz[2],
            cr, cg, cb, ca);
}

typedef HRESULT(WINAPI* D3DCompileFn)(LPCVOID, SIZE_T, LPCSTR,
                                      const D3D_SHADER_MACRO*, ID3DInclude*,
                                      LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

D3DCompileFn ResolveCompile() {
    static D3DCompileFn fn = nullptr;
    if (fn) return fn;
    HMODULE h = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!h) h = LoadLibraryA("d3dcompiler_43.dll");
    if (h) fn = (D3DCompileFn)GetProcAddress(h, "D3DCompile");
    return fn;
}

HRESULT MakePipeline(ID3D11Device* dev) {
    D3DCompileFn comp = ResolveCompile();
    if (!comp) return E_FAIL;

    const char* vs_src =
        "struct VSIn { float2 p : POSITION; float4 c : COLOR; };\n"
        "struct VSOut { float4 p : SV_POSITION; float4 c : COLOR; };\n"
        "cbuffer Buf : register(b0) { float2 vp; float2 pad; };\n"
        "VSOut main(VSIn i) {\n"
        "  VSOut o;\n"
        "  o.p = float4(i.p.x / vp.x * 2.0 - 1.0, 1.0 - i.p.y / vp.y * 2.0, 0.0, 1.0);\n"
        "  o.c = i.c;\n"
        "  return o;\n"
        "}\n";
    const char* ps_src =
        "struct PSIn { float4 p : SV_POSITION; float4 c : COLOR; };\n"
        "float4 main(PSIn i) : SV_Target { return i.c; }\n";

    ID3DBlob *vs_b = nullptr, *ps_b = nullptr, *err = nullptr;
    if (FAILED(comp(vs_src, strlen(vs_src), nullptr, nullptr, nullptr,
                    "main", "vs_4_0", 0, 0, &vs_b, &err)))
        return E_FAIL;
    if (FAILED(comp(ps_src, strlen(ps_src), nullptr, nullptr, nullptr,
                    "main", "ps_4_0", 0, 0, &ps_b, &err)))
        return E_FAIL;

    if (FAILED(dev->CreateVertexShader(vs_b->GetBufferPointer(), vs_b->GetBufferSize(),
                                       nullptr, &s_vs))) return E_FAIL;
    if (FAILED(dev->CreatePixelShader(ps_b->GetBufferPointer(), ps_b->GetBufferSize(),
                                      nullptr, &s_ps))) return E_FAIL;

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    dev->CreateInputLayout(layout, 2, vs_b->GetBufferPointer(), vs_b->GetBufferSize(), &s_il);

    D3D11_BUFFER_DESC vb = {};
    vb.ByteWidth = (UINT)(kMaxVerts * sizeof(Vtx));
    vb.Usage = D3D11_USAGE_DYNAMIC;
    vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dev->CreateBuffer(&vb, nullptr, &s_vb);

    D3D11_BUFFER_DESC cb = {};
    cb.ByteWidth = sizeof(CbData);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dev->CreateBuffer(&cb, nullptr, &s_cb);

    D3D11_RASTERIZER_DESC rs = {};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE;
    rs.DepthClipEnable = FALSE;
    dev->CreateRasterizerState(&rs, &s_rs);

    D3D11_BLEND_DESC bs = {};
    bs.AlphaToCoverageEnable = FALSE;
    bs.IndependentBlendEnable = FALSE;
    D3D11_RENDER_TARGET_BLEND_DESC& bt = bs.RenderTarget[0];
    bt.BlendEnable = TRUE;
    bt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bt.BlendOp = D3D11_BLEND_OP_ADD;
    bt.SrcBlendAlpha = D3D11_BLEND_ONE;
    bt.DestBlendAlpha = D3D11_BLEND_ONE;
    bt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev->CreateBlendState(&bs, &s_bs);

    vs_b->Release();
    ps_b->Release();
    return S_OK;
}

bool GetDeviceAndContext(IDXGISwapChain* sc, ID3D11Device** devOut,
                         ID3D11DeviceContext** ctxOut) {
    __try {
        IDXGIDevice* dxgiDev = nullptr;
        if (FAILED(sc->GetDevice(IID_PPV_ARGS(&dxgiDev))) || !dxgiDev) return false;
        HRESULT hr = dxgiDev->QueryInterface(IID_PPV_ARGS(devOut));
        dxgiDev->Release();
        if (FAILED(hr) || !*devOut) return false;
        (*devOut)->GetImmediateContext(ctxOut);
        return *ctxOut != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool GetBackbufferSize(IDXGISwapChain* sc, float& w, float& h) {
    __try {
        ID3D11Texture2D* tex = nullptr;
        if (FAILED(sc->GetBuffer(0, IID_PPV_ARGS(&tex))) || !tex) return false;
        D3D11_TEXTURE2D_DESC td{};
        tex->GetDesc(&td);
        tex->Release();
        w = (float)td.Width;
        h = (float)td.Height;
        return w > 0.f && h > 0.f;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Draw(IDXGISwapChain* sc, const std::vector<Vtx>& verts, float dim_x, float dim_y) {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    if (!GetDeviceAndContext(sc, &dev, &ctx)) return;

    if (!s_render_ok) {
        if (FAILED(MakePipeline(dev))) { dev->Release(); ctx->Release(); return; }
        s_render_ok = true;
    }

    __try {
        ID3D11Texture2D* tex = nullptr;
        if (FAILED(sc->GetBuffer(0, IID_PPV_ARGS(&tex))) || !tex) { dev->Release(); ctx->Release(); return; }
        ID3D11RenderTargetView* rtv = nullptr;
        if (FAILED(dev->CreateRenderTargetView(tex, nullptr, &rtv))) {
            tex->Release(); dev->Release(); ctx->Release(); return;
        }
        tex->Release();

        if (s_rtv && s_rtv != rtv) { s_rtv->Release(); }
        s_rtv = rtv;

        D3D11_VIEWPORT vp = { 0.f, 0.f, dim_x, dim_y, 0.f, 1.f };
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(s_rs);
        ctx->OMSetBlendState(s_bs, nullptr, 0xFFFFFFFF);
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->IASetInputLayout(s_il);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        UINT stride = sizeof(Vtx), offset = 0;
        ctx->IASetVertexBuffers(0, 1, &s_vb, &stride, &offset);

        D3D11_MAPPED_SUBRESOURCE mr{};
        if (SUCCEEDED(ctx->Map(s_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mr))) {
            memcpy(mr.pData, verts.data(), verts.size() * sizeof(Vtx));
            ctx->Unmap(s_vb, 0);
        }

        CbData cbd{};
        cbd.vp[0] = dim_x;
        cbd.vp[1] = dim_y;
        D3D11_MAPPED_SUBRESOURCE mc{};
        if (SUCCEEDED(ctx->Map(s_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mc))) {
            memcpy(mc.pData, &cbd, sizeof(cbd));
            ctx->Unmap(s_cb, 0);
        }

        ctx->VSSetConstantBuffers(0, 1, &s_cb);
        ctx->VSSetShader(s_vs, nullptr, 0);
        ctx->PSSetShader(s_ps, nullptr, 0);
        ctx->Draw((UINT)verts.size(), 0);

        ctx->VSSetShader(nullptr, nullptr, 0);
        ctx->PSSetShader(nullptr, nullptr, 0);
        ctx->IASetInputLayout(nullptr);
        ctx->RSSetState(nullptr);
        ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

        ctx->Release();
        dev->Release();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ctx->Release();
        dev->Release();
    }
}

void RenderFrame(IDXGISwapChain* sc, const float m[16], float dim_x, float dim_y,
                 float cr, float cg, float cb, float ca, size_t& drawn) {
    drawn = 0;
    std::vector<Vtx> verts;
    verts.reserve(60000);

    uintptr_t players = 0;
    for (const uintptr_t c : inmem::Children(s_dm)) {
        if (inmem::GetClass(c) == "Players") { players = c; break; }
    }
    if (!players) return;

    const uintptr_t local     = inmem::Rd(players + nofs::Players::LocalPlayer);
    const uintptr_t localChar = local ? inmem::CharacterOf(local) : 0;

    const std::vector<uintptr_t> plist = inmem::Children(players);
    for (const uintptr_t p : plist) {
        if (!p) continue;
        if (p == local) continue; // local rendered below when include_local
        const uintptr_t ch = inmem::CharacterOf(p);
        if (!ch || (!g_is_local && ch == localChar)) continue;
        for (const uintptr_t part : CachedParts(ch, false)) {
            PushBoxFromPart(part, m, dim_x, dim_y, cr, cg, cb, ca, verts);
            if (verts.size() >= kMaxVerts) break;
        }
        if (verts.size() >= kMaxVerts) break;
    }
    if (g_is_local && localChar && verts.size() < kMaxVerts) {
        for (const uintptr_t part : CachedParts(localChar, true)) {
            PushBoxFromPart(part, m, dim_x, dim_y, cr, cg, cb, ca, verts);
            if (verts.size() >= kMaxVerts) break;
        }
    }
    if (verts.empty()) return;

    Draw(sc, verts, dim_x, dim_y);
    drawn = verts.size();
}

} // namespace

void SetConfig(void* config) { s_cfg = static_cast<ncfg::Config*>(config); }

void OnPresent(IDXGISwapChain* sc) {
    if (!s_cfg || s_cfg->magic != ncfg::kMagic || !s_cfg->enabled) return;

    s_now_ms = NowMs();
    if (!Resolve()) return;
    if (s_cfg->status < ncfg::kStatusResolved)
        s_cfg->status = ncfg::kStatusResolved;

    float m[16]{};
    if (!ReadView(m)) return;

    float dim_x = 0.f, dim_y = 0.f;
    if (!GetBackbufferSize(sc, dim_x, dim_y)) return;

    g_is_local = s_cfg->include_local != 0;

    float ca = s_cfg->color[3];
    if (ca <= 0.f) ca = 0.012f;

    size_t drawn = 0;
    RenderFrame(sc, m, dim_x, dim_y, s_cfg->color[0], s_cfg->color[1],
                s_cfg->color[2], ca, drawn);
    if (drawn) {
        ++s_cfg->frames;
        s_cfg->last_parts = (uint32_t)drawn;
        if (s_cfg->status < ncfg::kStatusDrawing)
            s_cfg->status = ncfg::kStatusDrawing;
    }
}

} // namespace nchams
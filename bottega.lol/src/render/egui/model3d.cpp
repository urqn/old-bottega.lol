#define NOMINMAX  // Previene conflictos con min/max de Windows

#include <Windows.h>
#include "model3d.h"
#include "egui.h"
#include "egui_settings.h"
#include "egui_model.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
namespace fs = std::filesystem;

using namespace DirectX;

namespace egui {

    // ─────────────────────────────────────────────────────────────────────────────
    //  HLSL  — two-light model (key + fill) + rim
    // ─────────────────────────────────────────────────────────────────────────────
    static const char* k_shader = R"(
cbuffer CB : register(b0) {
    float4x4 gMVP;
    float4x4 gWorld;
    float3 gKeyLight;   float pad0;
    float3 gFillLight;  float pad1;
    float3 gCamPos;     float pad2;
};

Texture2D gDiffuse : register(t0);
SamplerState gSampler : register(s0);

struct VSIn { float3 pos : POSITION; float3 nor : NORMAL; float2 uv : TEXCOORD; };
struct PSIn { float4 pos : SV_POSITION; float3 nor : TEXCOORD0; float3 wp : TEXCOORD1; float2 uv : TEXCOORD2; };

PSIn VS(VSIn v) {
    PSIn o;
    o.pos = mul(float4(v.pos, 1), gMVP);
    o.nor = normalize(mul(float4(v.nor, 0), gWorld).xyz);
    o.wp  = mul(float4(v.pos, 1), gWorld).xyz;
    o.uv  = v.uv;
    return o;
}

float4 PS(PSIn p) : SV_Target {
    float3 N = normalize(p.nor);
    float3 V = normalize(gCamPos - p.wp);

    float  key  = saturate(dot(N, normalize(gKeyLight)));
    float3 kcol = float3(0.92, 0.90, 0.86) * (key * 0.80 + 0.35);

    float  fill  = saturate(dot(N, normalize(gFillLight)));
    float3 fcol  = float3(0.45, 0.50, 0.60) * fill * 0.30;

    float  rim   = pow(1.0 - saturate(dot(N, V)), 3.2) * 0.25;
    float3 rcol  = float3(0.65, 0.70, 0.90) * rim;

    float3 H    = normalize(normalize(gKeyLight) + V);
    float  spec = pow(saturate(dot(N, H)), 22.0) * 0.08;

    float3 lit  = (kcol + fcol) + rcol + spec;

    float4 tex = gDiffuse.Sample(gSampler, p.uv);
    return float4(tex.rgb * lit, 1.0f);
}
)";

    // ─────────────────────────────────────────────────────────────────────────────
    //  Procedural humanoid  (ellipsoid-based, no OBJ needed)
    // ─────────────────────────────────────────────────────────────────────────────
    static void append_ellipsoid(std::vector<Model3DRenderer::Vtx>& verts,
        std::vector<uint32_t>& inds,
        float tx, float ty, float tz,
        float sx, float sy, float sz,
        int stacks = 14, int slices = 20)
    {
        uint32_t base = (uint32_t)verts.size();
        for (int i = 0; i <= stacks; ++i) {
            float phi = XM_PI * i / stacks;
            float sp = sinf(phi), cp = cosf(phi);
            for (int j = 0; j <= slices; ++j) {
                float theta = 2.0f * XM_PI * j / slices;
                float nx = sp * cosf(theta), ny = cp, nz = sp * sinf(theta);
                XMVECTOR nv = XMVector3Normalize(XMVectorSet(nx / sx, ny / sy, nz / sz, 0));
                XMFLOAT3 fn; XMStoreFloat3(&fn, nv);
                Model3DRenderer::Vtx v;
                v.px = nx * sx + tx; v.py = ny * sy + ty; v.pz = nz * sz + tz;
                v.nx = fn.x;       v.ny = fn.y;       v.nz = fn.z;
                v.tu = (float)j / slices; v.tv = (float)i / stacks;
                verts.push_back(v);
            }
        }
        for (int i = 0; i < stacks; ++i)
            for (int j = 0; j < slices; ++j) {
                uint32_t a = base + i * (slices + 1) + j, b = a + 1;
                uint32_t c = base + (i + 1) * (slices + 1) + j, d = c + 1;
                inds.insert(inds.end(), { a,c,b, b,c,d });
            }
    }

    void Model3DRenderer::build_procedural() {
        cpu_verts.clear(); cpu_inds.clear();

        // ── Body ──────────────────────────────────────────────────────────────────
        append_ellipsoid(cpu_verts, cpu_inds, 0.000f, 0.580f, 0.000f, 0.215f, 0.215f, 0.215f); // head
        append_ellipsoid(cpu_verts, cpu_inds, 0.000f, 0.215f, 0.000f, 0.165f, 0.225f, 0.150f); // torso (slightly thicker)
        append_ellipsoid(cpu_verts, cpu_inds, 0.000f, 0.005f, 0.000f, 0.145f, 0.090f, 0.140f); // hip (slightly thicker)
        // ── Arms ──────────────────────────────────────────────────────────────────
        append_ellipsoid(cpu_verts, cpu_inds, -0.225f, 0.300f, 0.000f, 0.070f, 0.135f, 0.070f); // L upper arm
        append_ellipsoid(cpu_verts, cpu_inds, -0.250f, 0.095f, 0.000f, 0.063f, 0.115f, 0.063f); // L lower arm
        append_ellipsoid(cpu_verts, cpu_inds, -0.248f, -0.060f, 0.000f, 0.092f, 0.072f, 0.082f); // L cuff
        append_ellipsoid(cpu_verts, cpu_inds, 0.225f, 0.300f, 0.000f, 0.070f, 0.135f, 0.070f); // R upper arm
        append_ellipsoid(cpu_verts, cpu_inds, 0.250f, 0.095f, 0.000f, 0.063f, 0.115f, 0.063f); // R lower arm
        append_ellipsoid(cpu_verts, cpu_inds, 0.248f, -0.060f, 0.000f, 0.092f, 0.072f, 0.082f); // R cuff
        // ── Legs ──────────────────────────────────────────────────────────────────
        append_ellipsoid(cpu_verts, cpu_inds, -0.092f, -0.115f, 0.000f, 0.077f, 0.140f, 0.077f); // L upper leg
        append_ellipsoid(cpu_verts, cpu_inds, -0.092f, -0.320f, 0.000f, 0.070f, 0.125f, 0.070f); // L lower leg
        append_ellipsoid(cpu_verts, cpu_inds, -0.092f, -0.465f, 0.018f, 0.080f, 0.062f, 0.112f); // L foot
        append_ellipsoid(cpu_verts, cpu_inds, 0.092f, -0.115f, 0.000f, 0.077f, 0.140f, 0.077f); // R upper leg
        append_ellipsoid(cpu_verts, cpu_inds, 0.092f, -0.320f, 0.000f, 0.070f, 0.125f, 0.070f); // R lower leg
        append_ellipsoid(cpu_verts, cpu_inds, 0.092f, -0.465f, 0.018f, 0.080f, 0.062f, 0.112f); // R foot

        compute_bounds();
        mesh_ready = true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Bounding box del mesh — de aqui salen el centro y el encuadre
    //
    //  El radio horizontal es el mayor de los dos ejes del suelo (x y z) a
    //  proposito: al orbitar en yaw, la profundidad acaba pasando por el ancho de
    //  la pantalla, y midiendo solo x el modelo se salia por los lados a mitad de
    //  giro.
    // ─────────────────────────────────────────────────────────────────────────────
    void Model3DRenderer::compute_bounds() {
        if (cpu_verts.empty()) return;

        float mn_x = 1e9f, mn_y = 1e9f, mn_z = 1e9f;
        float mx_x = -1e9f, mx_y = -1e9f, mx_z = -1e9f;
        for (auto& v : cpu_verts) {
            mn_x = (std::min)(mn_x, v.px); mx_x = (std::max)(mx_x, v.px);
            mn_y = (std::min)(mn_y, v.py); mx_y = (std::max)(mx_y, v.py);
            mn_z = (std::min)(mn_z, v.pz); mx_z = (std::max)(mx_z, v.pz);
        }

        center_x = (mn_x + mx_x) * 0.5f;
        center_y = (mn_y + mx_y) * 0.5f;
        center_z = (mn_z + mx_z) * 0.5f;

        half_h = (std::max)((mx_y - mn_y) * 0.5f, 0.0001f);
        radius_xz = (std::max)({ (mx_x - mn_x) * 0.5f, (mx_z - mn_z) * 0.5f, 0.0001f });
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  OBJ Loader
    //  Supports: v, vn, vt, f (tri & quad), v//vn, v/vt/vn, v/vt, v
    // ─────────────────────────────────────────────────────────────────────────────
    bool Model3DRenderer::load_obj(const char* path) {
        std::ifstream disk_file(path, std::ios::binary | std::ios::ate);
        if (!disk_file.is_open()) {
            char buf[256];
            snprintf(buf, sizeof(buf), "[model3d] Cannot open OBJ: %s\n", path);
            OutputDebugStringA(buf);
            return false;
        }
        std::streamsize size = disk_file.tellg();
        disk_file.seekg(0, std::ios::beg);

        std::vector<char> buffer(size);
        if (!disk_file.read(buffer.data(), size)) return false;

        std::string base_dir;
        auto p = fs::path(path);
        auto parent = p.parent_path();
        if (!parent.empty()) base_dir = parent.string() + "\\";

        bool success = load_obj_from_memory(buffer.data(), size, base_dir.empty() ? nullptr : base_dir.c_str());
        if (success) {
            char buf[256];
            snprintf(buf, sizeof(buf), "[model3d] Loaded %s — %zu verts, %zu tris\n",
                path, cpu_verts.size(), cpu_inds.size() / 3);
            OutputDebugStringA(buf);
        }
        return success;
    }

    bool Model3DRenderer::load_obj_from_memory(const void* data, size_t size, const char* base_dir) {
        if (!data || size == 0) return false;
        std::string str_data((const char*)data, size);
        std::istringstream file(str_data);

        std::vector<XMFLOAT3> pos_arr, nor_arr;
        std::vector<XMFLOAT2> uv_arr;

        struct FaceVtx { int p, t, n; };

        auto parse_fv = [](const char* s, FaceVtx& fv) {
            fv = { 0,0,0 };
            int p = 0, t = 0, n = 0;
            if (sscanf(s, "%d/%d/%d", &p, &t, &n) == 3) { fv = { p,t,n }; }
            else if (sscanf(s, "%d//%d", &p, &n) == 2) { fv = { p,0,n }; }
            else if (sscanf(s, "%d/%d", &p, &t) == 2) { fv = { p,t,0 }; }
            else if (sscanf(s, "%d", &p) == 1) { fv = { p,0,0 }; }
            };

        struct FVHash {
            size_t operator()(const FaceVtx& f) const {
                return std::hash<long long>()(((long long)f.p << 20) | ((long long)f.t << 10) | f.n);
            }
        };
        struct FVEq {
            bool operator()(const FaceVtx& a, const FaceVtx& b) const {
                return a.p == b.p && a.t == b.t && a.n == b.n;
            }
        };
        std::unordered_map<FaceVtx, uint32_t, FVHash, FVEq> cache;

        cpu_verts.clear(); cpu_inds.clear();
        diffuse_path.clear();

        std::string mtllib_name;

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            std::string tok; ss >> tok;

            if (tok == "v") {
                XMFLOAT3 p; ss >> p.x >> p.y >> p.z; pos_arr.push_back(p);
            }
            else if (tok == "vn") {
                XMFLOAT3 n; ss >> n.x >> n.y >> n.z; nor_arr.push_back(n);
            }
            else if (tok == "vt") {
                XMFLOAT2 t; ss >> t.x >> t.y; uv_arr.push_back(t);
            }
            else if (tok == "mtllib") {
                std::string mtl; std::getline(ss, mtl);
                mtl.erase(0, mtl.find_first_not_of(" \t"));
                mtllib_name = mtl;
            }
            else if (tok == "f") {
                std::vector<FaceVtx> fverts;
                std::string word;
                while (ss >> word) {
                    FaceVtx fv; parse_fv(word.c_str(), fv); fverts.push_back(fv);
                }
                // Fan-triangulate (handles quads and n-gons)
                for (int i = 1; i + 1 < (int)fverts.size(); ++i) {
                    FaceVtx tri[3] = { fverts[0], fverts[i], fverts[i + 1] };
                    for (auto& fv : tri) {
                        auto it = cache.find(fv);
                        if (it != cache.end()) {
                            cpu_inds.push_back(it->second);
                        }
                        else {
                            uint32_t idx = (uint32_t)cpu_verts.size();
                            cache[fv] = idx;
                            cpu_inds.push_back(idx);

                            Vtx v = {};
                            if (fv.p > 0 && fv.p <= (int)pos_arr.size()) {
                                auto& p = pos_arr[fv.p - 1];
                                v.px = p.x; v.py = p.y; v.pz = p.z;
                            }
                            if (fv.n > 0 && fv.n <= (int)nor_arr.size()) {
                                auto& n = nor_arr[fv.n - 1];
                                v.nx = n.x; v.ny = n.y; v.nz = n.z;
                            }
                            if (fv.t > 0 && fv.t <= (int)uv_arr.size()) {
                                auto& t = uv_arr[fv.t - 1];
                                v.tu = t.x; v.tv = 1.0f - t.y;
                            }
                            cpu_verts.push_back(v);
                        }
                    }
                }
            }
        }

        if (cpu_verts.empty()) return false;

        // If no normals in OBJ, generate flat normals
        bool has_normals = false;
        for (auto& v : cpu_verts) if (v.nx || v.ny || v.nz) { has_normals = true; break; }
        if (!has_normals) {
            // Accumulate face normals
            std::vector<XMFLOAT3> acc(cpu_verts.size(), { 0,0,0 });
            for (int i = 0; i + 2 < (int)cpu_inds.size(); i += 3) {
                auto& A = cpu_verts[cpu_inds[i]];
                auto& B = cpu_verts[cpu_inds[i + 1]];
                auto& C = cpu_verts[cpu_inds[i + 2]];
                XMVECTOR ab = XMVectorSet(B.px - A.px, B.py - A.py, B.pz - A.pz, 0);
                XMVECTOR ac = XMVectorSet(C.px - A.px, C.py - A.py, C.pz - A.pz, 0);
                XMVECTOR fn = XMVector3Normalize(XMVector3Cross(ab, ac));
                XMFLOAT3 n; XMStoreFloat3(&n, fn);
                for (int k = 0; k < 3; ++k) {
                    auto& a = acc[cpu_inds[i + k]];
                    a.x += n.x; a.y += n.y; a.z += n.z;
                }
            }
            for (int i = 0; i < (int)cpu_verts.size(); ++i) {
                XMVECTOR nv = XMVector3Normalize(XMLoadFloat3(&acc[i]));
                XMFLOAT3 fn; XMStoreFloat3(&fn, nv);
                cpu_verts[i].nx = fn.x; cpu_verts[i].ny = fn.y; cpu_verts[i].nz = fn.z;
            }
        }

        // Auto-center and scale to unit height
        float mn_y = 1e9f, mx_y = -1e9f, mn_x = 1e9f, mx_x = -1e9f, mn_z = 1e9f, mx_z = -1e9f;
        float cx = 0, cy = 0, cz = 0;
        for (auto& v : cpu_verts) {
            mn_y = (std::min)(mn_y, v.py);
            mx_y = (std::max)(mx_y, v.py);
            mn_x = (std::min)(mn_x, v.px);
            mx_x = (std::max)(mx_x, v.px);
            mn_z = (std::min)(mn_z, v.pz);
            mx_z = (std::max)(mx_z, v.pz);
        }
        cx = (mn_x + mx_x) * 0.5f; cy = (mn_y + mx_y) * 0.5f; cz = (mn_z + mx_z) * 0.5f;
        float extent = (std::max)({ mx_y - mn_y, mx_x - mn_x, mx_z - mn_z }) * 0.5f;
        float inv = extent > 0.0f ? 1.0f / extent : 1.0f;
        for (auto& v : cpu_verts) {
            v.px = (v.px - cx) * inv;
            v.py = (v.py - cy) * inv;
            v.pz = (v.pz - cz) * inv;
        }

        // Un .obj que no deja ni un triangulo no vale como mesh: sin esto se daba
        // por cargado y el preview se quedaba en negro.
        if (cpu_verts.empty() || cpu_inds.empty()) return false;

        // Parse MTL to find diffuse texture
        if (base_dir && !mtllib_name.empty()) {
            std::string mtl_path = std::string(base_dir) + mtllib_name;
            std::ifstream mtl_file(mtl_path);
            if (mtl_file.is_open()) {
                std::string mtl_line;
                while (std::getline(mtl_file, mtl_line)) {
                    if (mtl_line.compare(0, 7, "map_Kd ") == 0 || mtl_line.compare(0, 7, "map_kd ") == 0) {
                        std::string tex_name = mtl_line.substr(7);
                        size_t pos = tex_name.find_first_not_of(" \t");
                        if (pos != std::string::npos) tex_name = tex_name.substr(pos);
                        pos = tex_name.find_last_not_of(" \t\r\n");
                        if (pos != std::string::npos) tex_name = tex_name.substr(0, pos + 1);
                        diffuse_path = std::string(base_dir) + tex_name;
                        break;
                    }
                }
            }
        }

        compute_bounds();
        mesh_ready = true;
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  GPU upload
    // ─────────────────────────────────────────────────────────────────────────────
    bool Model3DRenderer::upload_mesh(ID3D11Device* dev) {
        if (vb) { vb->Release(); vb = nullptr; }
        if (ib) { ib->Release(); ib = nullptr; }

        if (cpu_verts.empty()) return false;

        D3D11_BUFFER_DESC bd = {};
        D3D11_SUBRESOURCE_DATA sd = {};

        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.ByteWidth = (UINT)(cpu_verts.size() * sizeof(Vtx));
        sd.pSysMem = cpu_verts.data();
        if (FAILED(dev->CreateBuffer(&bd, &sd, &vb))) return false;

        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.ByteWidth = (UINT)(cpu_inds.size() * sizeof(uint32_t));
        sd.pSysMem = cpu_inds.data();
        if (FAILED(dev->CreateBuffer(&bd, &sd, &ib))) return false;

        index_count = (int)cpu_inds.size();
        return true;
    }

    bool Model3DRenderer::load_texture(ID3D11Device* dev, const char* path) {
        if (diffuse_srv) { diffuse_srv->Release(); diffuse_srv = nullptr; }
        FILE* f = nullptr;
        if (fopen_s(&f, path, "rb") != 0 || !f) return false;
        fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
        std::vector<unsigned char> buf(len);
        if (fread(buf.data(), 1, len, f) != (size_t)len) { fclose(f); return false; }
        fclose(f);
        return create_texture_from_memory(dev, buf.data(), buf.size(), &diffuse_srv);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Init pipeline
    // ─────────────────────────────────────────────────────────────────────────────
    bool Model3DRenderer::init(ID3D11Device* dev) {
        if (init_done) return true;

        // Modelo empotrado (egui_model.h) si lo hay; si no, la figura de siempre.
        if (!mesh_ready) {
            if (!egui_model_embedded() ||
                !load_obj_from_memory(egui_model_obj, sizeof(egui_model_obj)))
                build_procedural();
        }
        if (!upload_mesh(dev)) return false;

        // Compile shaders
        ID3DBlob* vsb = nullptr, * psb = nullptr, * err = nullptr;
        if (FAILED(D3DCompile(k_shader, strlen(k_shader), nullptr, nullptr, nullptr,
            "VS", "vs_5_0", 0, 0, &vsb, &err))) {
            if (err) { OutputDebugStringA((char*)err->GetBufferPointer()); err->Release(); }
            return false;
        }
        if (FAILED(D3DCompile(k_shader, strlen(k_shader), nullptr, nullptr, nullptr,
            "PS", "ps_5_0", 0, 0, &psb, &err))) {
            if (err) { OutputDebugStringA((char*)err->GetBufferPointer()); err->Release(); }
            vsb->Release(); return false;
        }
        if (FAILED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs))) {
            vsb->Release(); psb->Release(); return false;
        }
        if (FAILED(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps))) {
            vs->Release(); vs = nullptr; vsb->Release(); psb->Release(); return false;
        }

        D3D11_INPUT_ELEMENT_DESC ied[] = {
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0, 0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"NORMAL",  0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,    0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
        };
        if (FAILED(dev->CreateInputLayout(ied, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout))) {
            ps->Release(); ps = nullptr; vs->Release(); vs = nullptr;
            vsb->Release(); psb->Release(); return false;
        }
        vsb->Release(); psb->Release();

        // Constant buffer: 2x float4x4 + 3x float4 = 176 bytes
        {
            D3D11_BUFFER_DESC bd = {};
            bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bd.ByteWidth = 176; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(dev->CreateBuffer(&bd, nullptr, &cb))) {
                layout->Release(); layout = nullptr; ps->Release(); ps = nullptr;
                vs->Release(); vs = nullptr; return false;
            }
        }
        // Rasterizer
        {
            D3D11_RASTERIZER_DESC rd = {};
            rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_BACK;
            rd.DepthClipEnable = TRUE;
            if (FAILED(dev->CreateRasterizerState(&rd, &rs))) {
                cb->Release(); cb = nullptr; layout->Release(); layout = nullptr;
                ps->Release(); ps = nullptr; vs->Release(); vs = nullptr; return false;
            }
        }
        // Depth stencil
        {
            D3D11_DEPTH_STENCIL_DESC dsd = {};
            dsd.DepthEnable = TRUE;
            dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            dsd.DepthFunc = D3D11_COMPARISON_LESS;
            if (FAILED(dev->CreateDepthStencilState(&dsd, &dss))) {
                rs->Release(); rs = nullptr; cb->Release(); cb = nullptr;
                layout->Release(); layout = nullptr; ps->Release(); ps = nullptr;
                vs->Release(); vs = nullptr; return false;
            }
        }

        // Create sampler state
        {
            D3D11_SAMPLER_DESC sd = {};
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
            sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
            sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            sd.MaxAnisotropy = 1;
            sd.MinLOD = 0; sd.MaxLOD = D3D11_FLOAT32_MAX;
            if (FAILED(dev->CreateSamplerState(&sd, &sampler_state))) {
                dss->Release(); dss = nullptr; rs->Release(); rs = nullptr;
                cb->Release(); cb = nullptr; layout->Release(); layout = nullptr;
                ps->Release(); ps = nullptr; vs->Release(); vs = nullptr;
                return false;
            }
        }
        // Create 1x1 white fallback texture (so untextured models still light properly)
        {
            D3D11_SUBRESOURCE_DATA init = {};
            unsigned int white_pixel = 0xFFFFFFFF;
            init.pSysMem = &white_pixel;
            init.SysMemPitch = 4;
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            ID3D11Texture2D* white_tex = nullptr;
            ID3D11ShaderResourceView* white_srv = nullptr;
            if (SUCCEEDED(dev->CreateTexture2D(&td, &init, &white_tex)) &&
                SUCCEEDED(dev->CreateShaderResourceView(white_tex, nullptr, &white_srv))) {
                if (!diffuse_srv) diffuse_srv = white_srv; else white_srv->Release();
                white_tex->Release();
            }
        }
        // Load diffuse texture from OBJ's MTL reference
        if (!diffuse_path.empty()) {
            load_texture(dev, diffuse_path.c_str());
        }

        init_done = true;
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Render target resize
    // ─────────────────────────────────────────────────────────────────────────────
    bool Model3DRenderer::ensure_rt(ID3D11Device* dev, int w, int h) {
        if (rt_w == w && rt_h == h && rtv && dsv) return true;
        auto rel = [](auto*& p) {if (p) { p->Release(); p = nullptr; }};
        rel(rtv); rel(srv); rel(rt_tex); rel(dsv); rel(ds_tex);
        rt_w = w; rt_h = h;

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 1; td.ArraySize = 1;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;

        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &rt_tex))) { rt_w = rt_h = 0; return false; }
        if (FAILED(dev->CreateRenderTargetView(rt_tex, nullptr, &rtv))) { rel(rt_tex); rt_w = rt_h = 0; return false; }
        if (FAILED(dev->CreateShaderResourceView(rt_tex, nullptr, &srv))) { rel(rtv); rel(rt_tex); rt_w = rt_h = 0; return false; }

        td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &ds_tex))) { rel(srv); rel(rtv); rel(rt_tex); rt_w = rt_h = 0; return false; }
        if (FAILED(dev->CreateDepthStencilView(ds_tex, nullptr, &dsv))) { rel(ds_tex); rel(srv); rel(rtv); rel(rt_tex); rt_w = rt_h = 0; return false; }

        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Render
    // ─────────────────────────────────────────────────────────────────────────────
    struct alignas(16) CBData {
        XMFLOAT4X4 mvp;
        XMFLOAT4X4 world;
        XMFLOAT4   key;
        XMFLOAT4   fill;
        XMFLOAT4   cam;
    };

    void Model3DRenderer::render(ID3D11Device* dev, ID3D11DeviceContext* ctx,
        int w, int h, float mdx, float mdy, bool dragging)
    {
        if (w <= 0 || h <= 0) return;
        if (!init(dev)) return;
        if (!ensure_rt(dev, w, h)) return;

        if (dragging) {
            yaw_target += mdx * orbit_speed;
            pitch = ImClamp(pitch + mdy * orbit_speed, -pitch_limit, pitch_limit);
        }
        yaw = ImLerp(yaw, yaw_target, ImGui::GetIO().DeltaTime * 12.0f);

        // Save state
        ID3D11RenderTargetView* prev_rtv = nullptr;
        ID3D11DepthStencilView* prev_dsv = nullptr;
        ctx->OMGetRenderTargets(1, &prev_rtv, &prev_dsv);
        UINT nvp = 1; D3D11_VIEWPORT prev_vp = {};
        ctx->RSGetViewports(&nvp, &prev_vp);

        // Validate RT resources before using them
        if (!rtv || !dsv) { if (prev_rtv) prev_rtv->Release(); if (prev_dsv) prev_dsv->Release(); return; }

        // Clear & set RT
        ctx->ClearRenderTargetView(rtv, bg);
        ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        ctx->OMSetRenderTargets(1, &rtv, dsv);
        D3D11_VIEWPORT vp = { 0,0,(float)w,(float)h,0,1 };
        ctx->RSSetViewports(1, &vp);

        const float k_fov = XMConvertToRadians(38.0f);
        const float aspect = (float)w / h;

        XMMATRIX world = XMMatrixTranslation(-center_x, -center_y, -center_z) *
                         XMMatrixScaling(1.0f, 1.0f, thickness) *
                         XMMatrixRotationX(pitch) *
                         XMMatrixRotationY(yaw) *
                         XMMatrixTranslation(offset_x, offset_y, 0.0f);

        float dist = distance;
        if (auto_fit) {
            const float tan_half = tanf(k_fov * 0.5f);
            const float dv = half_h / ((std::max)(fill, 0.05f) * tan_half);
            const float dh = radius_xz / ((std::max)(fill, 0.05f) * tan_half * aspect);
            dist = (std::max)(dv, dh);
        }

        XMVECTOR cam_pos = XMVectorSet(0.0f, 0.0f, -dist, 1);
        XMMATRIX view = XMMatrixLookAtLH(
            cam_pos,
            XMVectorSet(0.0f, 0.0f, 0.0f, 1),
            XMVectorSet(0.0f, 1.0f, 0.0f, 0));
        XMMATRIX proj = XMMatrixPerspectiveFovLH(k_fov, aspect, 0.01f, 100.0f);
        XMMATRIX mvp = world * view * proj;

        // Update CB
        D3D11_MAPPED_SUBRESOURCE ms = {};
        ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
        CBData* d = (CBData*)ms.pData;
        XMStoreFloat4x4(&d->mvp, XMMatrixTranspose(mvp));
        XMStoreFloat4x4(&d->world, XMMatrixTranspose(world));
        d->key = XMFLOAT4(0.55f, 1.0f, -0.30f, 0);  // key light direction
        d->fill = XMFLOAT4(-0.40f, 0.3f, 0.80f, 0);  // fill light (behind/opposite)
        XMStoreFloat4(&d->cam, cam_pos);
        ctx->Unmap(cb, 0);

        // Bind diffuse texture + sampler
        ID3D11ShaderResourceView* tex_srv = diffuse_srv ? diffuse_srv : nullptr;
        ID3D11SamplerState* samp = sampler_state;
        ctx->PSSetShaderResources(0, 1, &tex_srv);
        ctx->PSSetSamplers(0, 1, &samp);

        // Draw
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->IASetInputLayout(layout);
        UINT stride = sizeof(Vtx), offset = 0;
        ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        ctx->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, &cb);
        ctx->PSSetConstantBuffers(0, 1, &cb);
        ctx->RSSetState(rs);
        ctx->OMSetDepthStencilState(dss, 0);
        ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        ctx->DrawIndexed((UINT)index_count, 0, 0);

        // Unbind SRV to avoid D3D11 warning about bound resource still in use
        tex_srv = nullptr;
        ctx->PSSetShaderResources(0, 1, &tex_srv);

        // Restore state
        ctx->OMSetRenderTargets(1, &prev_rtv, prev_dsv);
        ctx->RSSetViewports(1, &prev_vp);
        if (prev_rtv) prev_rtv->Release();
        if (prev_dsv) prev_dsv->Release();
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Shutdown
    // ─────────────────────────────────────────────────────────────────────────────
    void Model3DRenderer::shutdown() {
        auto rel = [](auto*& p) {if (p) { p->Release(); p = nullptr; }};
        rel(rtv); rel(srv); rel(rt_tex); rel(dsv); rel(ds_tex);
        rel(vs); rel(ps); rel(layout); rel(vb); rel(ib); rel(cb);
        rel(rs); rel(dss); rel(diffuse_srv); rel(sampler_state);
        init_done = false; rt_w = rt_h = 0; mesh_ready = false;
    }

} // namespace egui

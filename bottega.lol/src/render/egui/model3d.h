#pragma once
#include <cstdint>
#include <d3d11.h>
#include <string>
#include "imgui.h"
#include <vector>

namespace egui {

    struct Model3DRenderer {
        // D3D resources
        ID3D11Texture2D* rt_tex = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        ID3D11Texture2D* ds_tex = nullptr;
        ID3D11DepthStencilView* dsv = nullptr;
        ID3D11VertexShader* vs = nullptr;
        ID3D11PixelShader* ps = nullptr;
        ID3D11InputLayout* layout = nullptr;
        ID3D11Buffer* vb = nullptr;
        ID3D11Buffer* ib = nullptr;
        ID3D11Buffer* cb = nullptr;
        ID3D11ShaderResourceView* diffuse_srv = nullptr;
        ID3D11SamplerState* sampler_state = nullptr;
        ID3D11RasterizerState* rs = nullptr;
        ID3D11DepthStencilState* dss = nullptr;

        // State
        int   rt_w = 0, rt_h = 0;
        float yaw = 3.14f;
        float yaw_target = 3.14f;
        float pitch = 0.0f;
        float distance = 3.8f;
        float thickness = 1.0f;
        float offset_x = 0.0f, offset_y = 0.0f;
        bool  init_done = false;
        int   index_count = 0;

        float orbit_speed = 0.010f;
        float pitch_limit = 1.2f;

        bool  auto_fit = true;
        float fill = 0.88f;
        float center_x = 0.0f, center_y = 0.0f, center_z = 0.0f;
        float half_h = 1.0f;      // media altura del bounding box
        float radius_xz = 1.0f;   // radio horizontal (el mayor de x / z)

        float bg[4] = { 0.078f, 0.078f, 0.078f, 0.55f };

        // Texture path found from MTL during OBJ loading
        std::string diffuse_path;

        // CPU-side mesh (filled by load_obj or build_procedural)
        struct Vtx {
            float px, py, pz;  // position
            float nx, ny, nz;  // normal
            float tu, tv;      // texcoord
        };
        std::vector<Vtx>      cpu_verts;
        std::vector<uint32_t> cpu_inds;
        bool mesh_ready = false;

        bool  load_obj(const char* path);
        bool  load_obj_from_memory(const void* data, size_t size, const char* base_dir = nullptr);
        bool  load_texture(ID3D11Device* dev, const char* path);

        bool  init(ID3D11Device* dev);
        bool  ensure_rt(ID3D11Device* dev, int w, int h);
        void  render(ID3D11Device* dev, ID3D11DeviceContext* ctx,
            int w, int h, float mdx, float mdy, bool dragging);
        void  shutdown();

        ImTextureID texture() const { return (ImTextureID)srv; }

    private:
        void build_procedural();
        void compute_bounds();
        bool upload_mesh(ID3D11Device* dev);
    };

    inline Model3DRenderer g_model3d;

} // namespace egui

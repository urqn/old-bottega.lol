#pragma once
// egui_model.h
//
// Embedded 3D model for the preview panel.
//
// HOW TO USE IT
//   1. Take your model as a Wavefront .obj (triangles or quads; v / vn / vt / f
//      are supported, and the mesh gets normalized and centered on load).
//   2. Turn the file into a C byte array. Any of these works:
//         xxd -i model.obj > model.txt          (git bash / WSL)
//         python -c "print(','.join(hex(b) for b in open('model.obj','rb').read()))"
//   3. Paste the bytes between the markers below, replacing the placeholder.
//   4. Rebuild. That is all: the menu picks it up on its own, the preview panel
//      starts on the 3D tab, and the orbit / auto-fit keep working.
//
// Leave the placeholder untouched and the SDK keeps drawing its built-in
// procedural figure, so nothing breaks if you never fill this in.
//
// Textures are NOT read from here: an embedded .obj has no folder to resolve its
// .mtl against, so the model renders untextured. To texture it, load the image
// with egui::create_texture_from_memory() and assign it to
// egui::g_model3d.diffuse_srv after egui::initialize().

namespace egui {

    // ─────────────────────────────────────────────────────────────────────────
    //  AQUI COLOCAS TU MODELO 3D  —  PUT YOUR 3D MODEL HERE
    //  (bytes del .obj / bytes of the .obj file)
    // ─────────────────────────────────────────────────────────────────────────
    inline const unsigned char egui_model_obj[] = {
        0x00
    };
    // ─────────────────────────────────────────────────────────────────────────
    //  FIN DEL MODELO  —  END OF MODEL
    // ─────────────────────────────────────────────────────────────────────────

    // Placeholder = un byte. Cualquier .obj de verdad pasa de largo esto.
    inline bool egui_model_embedded() { return sizeof(egui_model_obj) > 64; }

} // namespace egui

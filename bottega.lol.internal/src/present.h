#pragma once
namespace nchams {
// Installs a class-vtable hook on IDXGISwapChain::Present (slot 8) and
// IDXGISwapChain1::Present1 (slot 22). Returns true once the hooks are live.
bool InstallPresentHook();
} // namespace nchams
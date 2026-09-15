#pragma once
struct IDXGISwapChain;

namespace nchams {
// Called from the Present hook (game render thread) every frame.
void OnPresent(IDXGISwapChain* swapChain);
// Provides the shared config struct (set by dllmain after mapping it).
void SetConfig(void* config);
} // namespace nchams
#pragma once
#include <cstdint>
#include <string>

namespace ManualMapper {

struct Result {
    bool             ok = false;
    std::string      error;
    std::uint64_t    module_base = 0;
};

// Injection flavour, selectable from the command line for diagnostics:
//   Map (default)  - full manual map + entry call via CreateRemoteThread.
//   LoadTest       - same DLL loaded the ordinary way (LoadLibraryExW). If the
//                    DLL runs here but not under Map, the manual-map path is
//                    at fault, not the payload.
//   ThreadTest     - map everything but only start a bare
//                    "xor eax,eax; ret" thread. If even that thread dies
//                    (exit code != 0), the environment kills injected threads.
enum class Mode { Map = 0, LoadTest = 1, ThreadTest = 2, SelfTest = 3, Apc = 4 };
extern Mode mode;

// Reads "<exe dir>\bottega.lol.internal.dll" and manually maps it into the
// target process: allocates the image, copies headers/sections, applies
// relocations, resolves imports against the target's module list, then calls
// the entrypoint via CreateRemoteThread. Does NOT use LoadLibrary in target.
Result MapInto(std::uint32_t target_pid);

// Loads the same DLL with LoadLibraryExW from a remote thread. Used by
// --loadtest to isolate manual-mapping problems from payload/CRT problems.
Result LoadByLoadLibrary(std::uint32_t target_pid);

} // namespace ManualMapper
#include "ManualMapper.h"

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "psapi.lib")

namespace ManualMapper {
namespace {

// Step-by-step logging; lands in the font of our AllocConsole window.
void LogMap(const char* fmt, ...) {
    static const DWORD t0 = GetTickCount();
    std::printf("[map] (+%lu ms) ", GetTickCount() - t0);
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::fflush(stdout);
}

bool ReadBytes(uintptr_t process_handle, uintptr_t addr, void* dst, size_t n) {
    SIZE_T done = 0;
    return ReadProcessMemory(reinterpret_cast<HANDLE>(process_handle),
                             reinterpret_cast<LPCVOID>(addr), dst, n, &done) &&
           done == n;
}

bool WriteBytes(uintptr_t process_handle, uintptr_t addr, const void* src, size_t n) {
    SIZE_T done = 0;
    return WriteProcessMemory(reinterpret_cast<HANDLE>(process_handle),
                              reinterpret_cast<LPVOID>(addr), src, n, &done) &&
           done == n;
}

std::string ReadRemoteStr(HANDLE proc, uintptr_t addr, size_t max_len = 256) {
    if (!addr) return {};
    char buf[512];
    std::string out;
    for (size_t off = 0; off < max_len; off += 512) {
        size_t n = std::min<size_t>(512, max_len - off);
        if (!ReadBytes((uintptr_t)proc, addr + off, buf, n)) break;
        for (size_t i = 0; i < n; ++i) {
            if (!buf[i]) return out;
            out.push_back(buf[i]);
        }
        if (out.size() >= max_len) break;
    }
    return out;
}

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    w.pop_back();
    return w;
}

std::string ToLower(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = (char)tolower((unsigned char)c);
    return r;
}

bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out) {
    std::ifstream f(path.c_str(), std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;
    const auto size = f.tellg();
    if (size <= 0) return false;
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    f.close();
    out = std::move(data);
    return true;
}

// Builds a map of the target's loaded modules by lowercased base-name.
std::unordered_map<std::string, uintptr_t> EnumerateTargetModules(HANDLE proc) {
    std::unordered_map<std::string, uintptr_t> map;
    DWORD needed = 0;
    std::vector<HMODULE> mods(1024);
    if (!EnumProcessModulesEx(proc, mods.data(), (DWORD)(mods.size() * sizeof(HMODULE)),
                              &needed, LIST_MODULES_ALL)) {
        mods.resize(needed / sizeof(HMODULE));
        if (!EnumProcessModulesEx(proc, mods.data(), (DWORD)(mods.size() * sizeof(HMODULE)),
                                  &needed, LIST_MODULES_ALL))
            return map;
    }
    const size_t count = needed / sizeof(HMODULE);
    for (size_t i = 0; i < count; ++i) {
        wchar_t name[MAX_PATH] = {};
        GetModuleBaseNameW(proc, mods[i], name, MAX_PATH);
        if (!name[0]) continue;
        std::wstring wname(name);
        for (wchar_t& c : wname) c = (wchar_t)tolower(c);
        map[std::string(wname.begin(), wname.end())] = (uintptr_t)mods[i];
    }
    return map;
}

DWORD GetImageSize(const std::vector<uint8_t>& raw) {
    if (raw.size() < sizeof(IMAGE_DOS_HEADER)) return 0;
    const IMAGE_DOS_HEADER* dh = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
    if (dh->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    if (dh->e_lfanew <= 0 || (size_t)dh->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > raw.size())
        return 0;
    const IMAGE_NT_HEADERS64* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS64*>(raw.data() + dh->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    return nt->OptionalHeader.SizeOfImage;
}

// ==== Local module enumeration + RVA-based import resolution ================
struct LocalModule {
    uintptr_t base = 0;
    size_t    size = 0;
};

// Snapshots THIS process' modules, keyed by lowercased base name.
std::unordered_map<std::string, LocalModule> EnumerateLocalModules() {
    std::unordered_map<std::string, LocalModule> map;
    DWORD needed = 0;
    std::vector<HMODULE> mods(1024);
    if (!EnumProcessModulesEx(GetCurrentProcess(), mods.data(),
                              (DWORD)(mods.size() * sizeof(HMODULE)), &needed,
                              LIST_MODULES_ALL)) {
        mods.resize(needed / sizeof(HMODULE));
        if (!EnumProcessModulesEx(GetCurrentProcess(), mods.data(),
                                  (DWORD)(mods.size() * sizeof(HMODULE)), &needed,
                                  LIST_MODULES_ALL)) return map;
    }
    const size_t count = needed / sizeof(HMODULE);
    for (size_t i = 0; i < count; ++i) {
        wchar_t name[MAX_PATH] = {};
        GetModuleBaseNameW(GetCurrentProcess(), mods[i], name, MAX_PATH);
        if (!name[0]) continue;
        MODULEINFO mi{};
        if (!GetModuleInformation(GetCurrentProcess(), mods[i], &mi, sizeof(mi))) continue;
        std::wstring wname(name);
        for (wchar_t& c : wname) c = (wchar_t)tolower(c);
        LocalModule lm;
        lm.base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
        lm.size = mi.SizeOfImage;
        map[std::string(wname.begin(), wname.end())] = lm;
    }
    return map;
}

// Resolves an IAT slot for the target by computing the symbol's RVA in THIS
// process and transplanting it. GetProcAddress resolves forwarders for us
// (api-ms-win-crt-* stubs, VCRUNTIME140!memcpy -> ucrtbase, ...), so we find
// which of our own loaded modules actually holds the returned address and use
// THAT module's base for the RVA - the RVA is stable across copies of the same
// build.
uintptr_t ResolveRemoteImport(HANDLE proc,
                              const std::unordered_map<std::string, uintptr_t>& target_mods,
                              const std::string& module_name, uintptr_t proc_addr,
                              const std::string& proc_name, bool by_ordinal) {
    (void)proc;
    const std::string key = module_name + "!" + proc_name;
    std::string own;
    uintptr_t own_base = 0;
    size_t own_size = 0;
    int stage = 0; // 1 local-mod, 2 GetProcAddress, 3 owner, 4 rva, 5 target

    do {
        if (proc_name.empty() && !by_ordinal) { stage = 6; break; }

        HMODULE local_mod = GetModuleHandleW(ToWide(module_name).c_str());
        if (!local_mod) local_mod = LoadLibraryW(ToWide(module_name).c_str());
        if (!local_mod) { stage = 1; break; }

        FARPROC f = by_ordinal
            ? GetProcAddress(local_mod, reinterpret_cast<LPCSTR>(proc_addr & 0xFFFF))
            : GetProcAddress(local_mod, proc_name.c_str());
        if (!f) { stage = 2; break; }
        const uintptr_t addr = reinterpret_cast<uintptr_t>(f);

        // Snapshot per-call: modules may load mid-resolution.
        const auto local_mods = EnumerateLocalModules();
        for (const auto& kv : local_mods) {
            const LocalModule& lm = kv.second;
            if (addr >= lm.base && addr < lm.base + lm.size) {
                own = kv.first; own_base = lm.base; own_size = lm.size;
                break;
            }
        }
        if (own.empty() || own_base == 0 || own_size == 0) { stage = 3; break; }

        const uintptr_t rva = addr - own_base;
        if (rva >= own_size) { stage = 4; break; }
        const auto it = target_mods.find(own);
        if (it == target_mods.end()) { stage = 5; break; }
        return it->second + rva;
    } while (false);

    if (stage) {
        static std::unordered_map<std::string, bool> reported;
        if (!reported[key] && !module_name.empty()) {
            reported[key] = true;
            LogMap("  - resolve fail %s stage=%d owner=%s (has %zu target mods)\n",
                   key.c_str(), stage, own.c_str(), target_mods.size());
            if (stage == 5) {
                int shown = 0;
                for (const auto& kv : target_mods) {
                    const std::string& n = kv.first;
                    if (n.find("crt") != std::string::npos ||
                        n.find("msvcp") != std::string::npos ||
                        n.find("api-ms") != std::string::npos ||
                        n == own) {
                        if (shown++ < 40)
                            LogMap("      target has: %s -> 0x%p\n",
                                   n.c_str(), reinterpret_cast<void*>(kv.second));
                    }
                }
            }
        }
    }
    return 0;
}

// ==== Remote crash-address catcher =========================================
// Installs a stripped-down UnhandledExceptionFilter in the target BEFORE we
// spawn the entry/load thread. If anything dies with an unhandled exception
// the filter records {code, address} into a page we can read afterwards and
// swallows the exception, so the thread simply terminates (no WER popup) and
// we finally learn WHERE things die. The filter is a hand-assembled naked x64
// blob (no SEH, no CRT, no unwinding) so it works from memory that belongs to
// no registered module.
bool InstallCrashCatcher(HANDLE proc, uintptr_t& report_page) {
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC suef = k32 ? GetProcAddress(k32, "SetUnhandledExceptionFilter") : nullptr;
    if (!suef) return false;

    void* page = VirtualAllocEx(proc, nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE,
                                PAGE_EXECUTE_READWRITE);
    if (!page) return false;
    const uintptr_t p = reinterpret_cast<uintptr_t>(page);

    // Report header at +0x40: magic(u32) pad(u32) code(u64) addr(u64) state(u64).
    const uintptr_t hdr = p + 0x40;
    const uint64_t dst_code  = hdr + 8;
    const uint64_t dst_addr  = hdr + 16;
    const uint64_t dst_state = hdr + 24;
    {
        uint64_t head[2] = { 0 } ;
        const uint32_t magic = 0xCAFE110C;
        if (!WriteProcessMemory(proc, reinterpret_cast<LPVOID>(hdr), &magic, 4, nullptr) ||
            !WriteProcessMemory(proc, reinterpret_cast<LPVOID>(hdr + 4), &head[0], 4, nullptr) ||
            !WriteProcessMemory(proc, reinterpret_cast<LPVOID>(hdr + 8), head, 16, nullptr)) {
            VirtualFreeEx(proc, page, 0, MEM_RELEASE);
            return false;
        }
    }

    // Filter blob at +0x100. rcx = EXCEPTION_POINTERS*; record->ExceptionCode at
    // +0, record->ExceptionAddress at +16.
    const uint8_t blob[] = {
        0x48, 0x8B, 0x01,                                      // mov rax,[rcx]
        0x49, 0x8B, 0x40, 0x10,                                // mov r8,[rax+16]
        0x48, 0x8B, 0x08,                                      // mov rcx,[rax]
        0x48, 0xB8,                                            // mov rax, imm64
        (uint8_t)(dst_code), (uint8_t)(dst_code >> 8), (uint8_t)(dst_code >> 16),
        (uint8_t)(dst_code >> 24), (uint8_t)(dst_code >> 32), (uint8_t)(dst_code >> 40),
        (uint8_t)(dst_code >> 48), (uint8_t)(dst_code >> 56),
        0x48, 0x89, 0x08,                                      // mov [rax],rcx
        0x48, 0xB8,                                            // mov rax, imm64
        (uint8_t)(dst_addr), (uint8_t)(dst_addr >> 8), (uint8_t)(dst_addr >> 16),
        (uint8_t)(dst_addr >> 24), (uint8_t)(dst_addr >> 32), (uint8_t)(dst_addr >> 40),
        (uint8_t)(dst_addr >> 48), (uint8_t)(dst_addr >> 56),
        0x4C, 0x89, 0x00,                                      // mov [rax],r8
        0x48, 0xB8,                                            // mov rax, imm64
        (uint8_t)(dst_state), (uint8_t)(dst_state >> 8), (uint8_t)(dst_state >> 16),
        (uint8_t)(dst_state >> 24), (uint8_t)(dst_state >> 32), (uint8_t)(dst_state >> 40),
        (uint8_t)(dst_state >> 48), (uint8_t)(dst_state >> 56),
        0xC7, 0x00, 0x01, 0x00, 0x00, 0x00,                    // mov dword[rax],1
        0xB8, 0x01, 0x00, 0x00, 0x00,                          // mov eax, EXCEPTION_EXECUTE_HANDLER
        0xC3                                                   // ret
    };
    const uintptr_t blob_addr = p + 0x100;
    if (!WriteProcessMemory(proc, reinterpret_cast<LPVOID>(blob_addr), blob,
                            sizeof(blob), nullptr)) {
        VirtualFreeEx(proc, page, 0, MEM_RELEASE);
        return false;
    }

    HANDLE t = CreateRemoteThread(proc, nullptr, 0,
                                  reinterpret_cast<LPTHREAD_START_ROUTINE>(suef),
                                  reinterpret_cast<LPVOID>(blob_addr), 0, nullptr);
    if (t) {
        WaitForSingleObject(t, 3000);
        CloseHandle(t);
    }
    LogMap("crash catcher armed (filter at 0x%p, report at 0x%p)\n",
           reinterpret_cast<void*>(blob_addr), reinterpret_cast<void*>(hdr));
    report_page = reinterpret_cast<uintptr_t>(page);
    return true;
}

// Reads and explains a crash report produced by the catcher above.
void ReadCrashReport(HANDLE proc, uintptr_t page) {
    struct { uint32_t magic; uint32_t pad; uint64_t code; uint64_t addr; uint64_t state; } hdr{};
    if (!ReadBytes((uintptr_t)proc, page + 0x40, &hdr, sizeof(hdr))) {
        LogMap("crash report unreadable\n");
        return;
    }
    if (hdr.magic != 0xCAFE110C) {
        LogMap("crash report magic missing (catcher never installed?)\n");
        return;
    }
    if (!hdr.state) {
        LogMap("crash catcher saw no unhandled exception\n");
        return;
    }
    LogMap("!!! unhandled exception: code 0x%08X at 0x%08X%08X\n",
           (uint32_t)hdr.code, (uint32_t)(hdr.addr >> 32), (uint32_t)hdr.addr);
    const auto target_mods = EnumerateTargetModules(proc);
    uintptr_t best_base = 0;
    std::string best;
    for (const auto& kv : target_mods) {
        if (kv.second <= hdr.addr && kv.second > best_base) {
            best_base = kv.second;
            best = kv.first;
        }
    }
    if (best_base) {
        LogMap("    -> %s + 0x%llX\n", best.c_str(),
               (unsigned long long)(hdr.addr - best_base));
    } else {
        LogMap("    -> address is not inside a loaded module\n");
    }
}

// Queues a bootstrap APC onto every thread of the target process. Because APC
// routines run on EXISTING threads, this bypasses anti-cheats that terminate
// remotely-created threads. We suspend-queeue-resume so threads already parked
// in an alertable wait pick the APC up immediately; the rest pick it up the
// next time they reach an alertable wait (or never - which the one-shot guard
// in the stub makes harmless).
size_t QueueApcsToProcess(HANDLE proc, uintptr_t stub_mem) {
    const DWORD pid = GetProcessId(proc);
    size_t queued = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == 0) continue;
            HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                       THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION |
                                       THREAD_SET_INFORMATION,
                                   FALSE, te.th32ThreadID);
            if (!th) continue;
            // Parking briefly: if the thread sits in an alertable wait, the APC
            // delivers on resume. Threads in non-alertable waits keep it queued.
            SuspendThread(th);
            const BOOL q = QueueUserAPC(reinterpret_cast<PAPCFUNC>(stub_mem), th, 0);
            (void)ResumeThread(th);
            if (q) ++queued;
            CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return queued;
}

} // namespace

Mode mode = Mode::Map;

Result LoadByLoadLibrary(std::uint32_t target_pid) {
    Result res;

    wchar_t exe_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::wstring dll_path(exe_path);
    const size_t slash = dll_path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dll_path.resize(slash + 1);
    dll_path += L"bottega.lol.internal.dll";

    if (GetFileAttributesW(dll_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        LogMap("could not open %S (build it first)\n", dll_path.c_str());
        res.error = "could not open bottega.lol.internal.dll (build it first)";
        return res;
    }

    HANDLE proc = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
                                  PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION,
                              FALSE, target_pid);
    if (!proc || proc == INVALID_HANDLE_VALUE) {
        LogMap("OpenProcess(pid %u) failed (last error %lu)\n", target_pid, GetLastError());
        res.error = "open process failed";
        return res;
    }

    const size_t path_bytes = (dll_path.size() + 1) * sizeof(wchar_t);
    void* path_mem = VirtualAllocEx(proc, nullptr, path_bytes,
                                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!path_mem) {
        res.error = "path alloc failed";
        CloseHandle(proc);
        return res;
    }
    SIZE_T wrote = 0;
    if (!WriteProcessMemory(proc, path_mem, dll_path.c_str(), path_bytes, &wrote)) {
        res.error = "path write failed";
        VirtualFreeEx(proc, path_mem, 0, MEM_RELEASE);
        CloseHandle(proc);
        return res;
    }

    // kernel32 maps at the same base in every process, so its exported
    // LoadLibraryExW can be invoked from a remote thread directly.
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC loadlib = k32 ? GetProcAddress(k32, "LoadLibraryExW") : nullptr;
    if (!loadlib) {
        res.error = "kernel32!LoadLibraryExW not found locally";
        VirtualFreeEx(proc, path_mem, 0, MEM_RELEASE);
        CloseHandle(proc);
        return res;
    }
    LogMap("loading %S via kernel32!LoadLibraryExW at 0x%p\n",
           dll_path.c_str(), reinterpret_cast<void*>(loadlib));

    uintptr_t crash_page = 0;
    InstallCrashCatcher(proc, crash_page);

    HANDLE thread = CreateRemoteThread(
        proc, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(loadlib),
        path_mem, 0, nullptr);
    DWORD exit_code = 0;
    if (thread) {
        // LoadLibrary can take a while (CRT DLLs, TLS, etc).
        if (WaitForSingleObject(thread, 15000) == WAIT_OBJECT_0)
            GetExitCodeThread(thread, &exit_code);
        CloseHandle(thread);
    } else {
        LogMap("CreateRemoteThread failed (last error %lu)\n", GetLastError());
    }

    VirtualFreeEx(proc, path_mem, 0, MEM_RELEASE);
    if (crash_page) {
        ReadCrashReport(proc, crash_page);
        VirtualFreeEx(proc, reinterpret_cast<LPVOID>(crash_page), 0, MEM_RELEASE);
    }
    CloseHandle(proc);

    // A DLL LoadLibrary returns its HMODULE; NULL means it failed to initialize
    // (missing import, init exception swallowed by loader, etc).
    LogMap("LoadLibraryExW returned handle 0x%08X%08X\n",
           (uint32_t)(exit_code >> 32), (uint32_t)exit_code);
    res.ok = exit_code != 0;
    res.module_base = res.ok ? (uint64_t)exit_code : 0;
    res.error = res.ok ? std::string() : "LoadLibraryExW failed in the target";
    return res;
}

Result MapInto(std::uint32_t target_pid) {
    Result res;

    wchar_t exe_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::wstring dll_path(exe_path);
    const size_t slash = dll_path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dll_path.resize(slash + 1);
    dll_path += L"bottega.lol.internal.dll";

    std::vector<uint8_t> raw;
    if (!ReadFileBytes(dll_path, raw)) {
        LogMap("could not open %S (build it first)\n", dll_path.c_str());
        res.error = "could not open bottega.lol.internal.dll (build it first)";
        return res;
    }
    LogMap("reading %S (%zu bytes)\n", dll_path.c_str(), raw.size());
    if (raw.size() < sizeof(IMAGE_DOS_HEADER)) {
        res.error = "invalid dll file";
        return res;
    }

    const IMAGE_DOS_HEADER* dh = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
    if (dh->e_magic != IMAGE_DOS_SIGNATURE || dh->e_lfanew <= 0 ||
        (size_t)dh->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > raw.size()) {
        res.error = "invalid PE headers";
        return res;
    }
    const IMAGE_NT_HEADERS64* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS64*>(raw.data() + dh->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
        res.error = "not an x64 module";
        return res;
    }
    if (nt->FileHeader.Characteristics & IMAGE_FILE_RELOCS_STRIPPED) {
        res.error = "module has no relocation table";
        return res;
    }

    const IMAGE_OPTIONAL_HEADER64& oh = nt->OptionalHeader;
    LogMap("PE ok - image size 0x%X, %u sections\n", oh.SizeOfImage,
           nt->FileHeader.NumberOfSections);

    HANDLE proc = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
                                  PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION,
                              FALSE, target_pid);
    if (!proc || proc == INVALID_HANDLE_VALUE) {
        LogMap("OpenProcess(pid %u) failed (last error %lu)\n", target_pid, GetLastError());
        res.error = "open process failed";
        return res;
    }
    LogMap("opened process %u\n", target_pid);

    const uintptr_t image_base =
        (uintptr_t)VirtualAllocEx(proc, nullptr, oh.SizeOfImage,
                                  MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!image_base) {
        res.error = "VirtualAllocEx failed";
        CloseHandle(proc);
        return res;
    }
    LogMap("remote image at 0x%p (allocated %u bytes)\n",
           reinterpret_cast<void*>(image_base), oh.SizeOfImage);

    res.module_base = image_base;

    // Headers + sections.
    if (!WriteBytes((uintptr_t)proc, image_base, raw.data(), oh.SizeOfHeaders)) {
        res.error = "header write failed";
        CloseHandle(proc);
        return res;
    }

    // Sections. First section starts right after the optional header.
    const uint32_t sec_off = 4 + sizeof(IMAGE_FILE_HEADER) + nt->FileHeader.SizeOfOptionalHeader;
    const IMAGE_SECTION_HEADER* first =
        reinterpret_cast<const IMAGE_SECTION_HEADER*>(raw.data() + dh->e_lfanew + sec_off);
    const size_t sec_count = nt->FileHeader.NumberOfSections;
    size_t written = 0;
    for (WORD i = 0; i < sec_count; ++i) {
        const IMAGE_SECTION_HEADER& sec = first[i];
        const size_t file_off = sec.PointerToRawData;
        if (file_off >= raw.size()) continue;
        const size_t size = std::min<size_t>(sec.SizeOfRawData, raw.size() - file_off);
        if (size == 0) continue;
        if (!WriteBytes((uintptr_t)proc, image_base + sec.VirtualAddress,
                        raw.data() + file_off, size)) {
            res.error = "section write failed";
            CloseHandle(proc);
            return res;
        }
        ++written;
    }
    LogMap("headers + %zu sections written\n", written);

    // Relocations (x64 -> type 3, DIR64).
    const uint32_t reloc_rva = oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
    const uint32_t reloc_sz  = oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
    size_t fixups = 0;
    if (reloc_rva && reloc_sz) {
        uintptr_t cur = image_base + reloc_rva;
        const uintptr_t reloc_end = image_base + reloc_rva + reloc_sz;
        while (cur + sizeof(IMAGE_BASE_RELOCATION) <= reloc_end) {
            IMAGE_BASE_RELOCATION blk{};
            if (!ReadBytes((uintptr_t)proc, cur, &blk, sizeof(blk))) break;
            if (!blk.SizeOfBlock) break;
            const size_t count = (blk.SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2;
            const uintptr_t block_base = image_base + blk.VirtualAddress;
            const int64_t delta = (int64_t)(image_base - oh.ImageBase);
            for (size_t j = 0; j < count; ++j) {
                uint16_t ent = 0;
                if (!ReadBytes((uintptr_t)proc, cur + sizeof(blk) + j * 2, &ent, 2)) break;
                const int type = (ent >> 12) & 0xF;
                const uintptr_t va = block_base + (ent & 0xFFF);
                if (type == IMAGE_REL_BASED_DIR64) {
                    uint64_t v = 0;
                    if (!ReadBytes((uintptr_t)proc, va, &v, 8)) continue;
                    v += delta;
                    WriteBytes((uintptr_t)proc, va, &v, 8);
                    ++fixups;
                }
            }
            cur += blk.SizeOfBlock;
        }
    }
    LogMap("relocations applied (%zu)\n", fixups);

    // Imports: resolve function addresses from the target's loaded modules by
    // computing the RVAs from our own (same-version) loaded copies.
    const IMAGE_DATA_DIRECTORY& imp_dir =
        oh.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imp_dir.Size && imp_dir.VirtualAddress) {
        const auto target_mods = EnumerateTargetModules(proc);
        LogMap("target has %zu loaded modules for import resolution\n",
               target_mods.size());
        const uintptr_t imp_cur = image_base + imp_dir.VirtualAddress;
        const uintptr_t imp_end = imp_cur + imp_dir.Size;
        size_t thunks = 0; size_t modules = 0; size_t failed = 0;
        for (uintptr_t cur = imp_cur; cur + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= imp_end;
             cur += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
            IMAGE_IMPORT_DESCRIPTOR desc{};
            if (!ReadBytes((uintptr_t)proc, cur, &desc, sizeof(desc))) break;
            if (!desc.Name && !desc.FirstThunk) break;
            if (!desc.FirstThunk) continue;
            ++modules;
            const std::string module_name = ReadRemoteStr(proc, image_base + desc.Name);
            if (module_name.empty()) continue;

            const uintptr_t thunk_array = image_base + desc.FirstThunk;
            const uintptr_t orig_array  = desc.OriginalFirstThunk
                ? image_base + desc.OriginalFirstThunk : thunk_array;
            for (size_t i = 0; i < 4096; ++i) {
                uint64_t thunk = 0;
                uint64_t orig  = 0;
                if (!ReadBytes((uintptr_t)proc, orig_array + i * 8, &orig, 8)) break;
                if (!orig) break;
                if (!ReadBytes((uintptr_t)proc, thunk_array + i * 8, &thunk, 8)) break;
                if (!thunk) break;

                const bool by_ordinal = (orig & IMAGE_ORDINAL_FLAG64) != 0;
                std::string func_name;
                if (!by_ordinal) {
                    const uint32_t hint_rva = (uint32_t)(orig & 0x7FFFFFFF);
                    func_name = ReadRemoteStr(proc, image_base + hint_rva + 2, 128);
                }
                const uintptr_t target_addr = ResolveRemoteImport(
                    proc, target_mods, module_name,
                    by_ordinal ? (orig & 0xFFFF) : 0, func_name, by_ordinal);
                if (!target_addr) {
                    const std::string what =
                        by_ordinal ? "#" + std::to_string(orig & 0xFFFF) : func_name;
                    if (failed < 20)
                        LogMap("  - unresolved: %s!%s\n",
                               module_name.c_str(), what.c_str());
                    ++failed;
                    continue;
                }
                WriteBytes((uintptr_t)proc, thunk_array + i * 8, &target_addr, 8);
                ++thunks;
            }
        }
        LogMap("resolved %zu imports across %zu modules (%zu unresolved)\n",
               thunks, modules, failed);
    }

    // Call DllMain(DLL_PROCESS_ATTACH). The entry point (DllMainCRTStartup) must
    // see (hModule, DLL_PROCESS_ATTACH, lpReserved) in RCX/RDX/R8, so we build a
    // tiny stub that sets all three registers then jumps to the entry.
    //
    // Delivery differs per mode:
    //   Map/LoadTest: a fresh remote thread via CreateRemoteThread (blocked by
    //                 Roblox anti-cheat, kept for the selftest path).
    //   ThreadTest:   no entry at all, bare "xor eax,eax; ret".
    //   Apc:          no new thread - the stub is queued as an APC onto the
    //                 target's EXISTING threads, so the kernel's thread-creation
    //                 watching never fires. A one-shot guard (lock bts on a flag
    //                 page) makes the first serviced APC run DllMain; later ones
    //                 no-op.
    uintptr_t crash_page = 0;
    if (mode != Mode::ThreadTest && mode != Mode::Apc)
        InstallCrashCatcher(proc, crash_page);
    const uintptr_t entry = image_base + oh.AddressOfEntryPoint;

    // Shared "call DllMain" block: 42 bytes.
    std::vector<uint8_t> jump_block;
    {
        const auto st_emit = [&](const void* p, size_t n) {
            const uint8_t* b = reinterpret_cast<const uint8_t*>(p);
            jump_block.insert(jump_block.end(), b, b + n);
        };
        const uint8_t rex_cx[] = { 0x48, 0xB9 };
        const uint8_t rex_dx[] = { 0x48, 0xBA };
        const uint8_t rex_r8[] = { 0x49, 0xB8 };
        const uint8_t rex_ax[] = { 0x48, 0xB8 };
        const uint8_t jmp_ax[] = { 0xFF, 0xE0 };
        const uintptr_t reason = 1; // DLL_PROCESS_ATTACH
        const uintptr_t lpreserved = 0;
        st_emit(rex_cx, 2); st_emit(&image_base, 8);
        st_emit(rex_dx, 2); st_emit(&reason, 8);
        st_emit(rex_r8, 2); st_emit(&lpreserved, 8);
        st_emit(rex_ax, 2); st_emit(&entry, 8);
        st_emit(jmp_ax, 2);
    }

    std::vector<uint8_t> stub;
    uintptr_t flag_page = 0;
    if (mode == Mode::ThreadTest) {
        stub = { 0x33, 0xC0, 0xC3 }; // xor eax,eax; ret
        LogMap("thread-test: running a bare thread (entry is NOT called)\n");
    } else if (mode == Mode::Apc) {
        void* fp = VirtualAllocEx(proc, nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_READWRITE);
        if (!fp) {
            res.error = "apc flag alloc failed";
            CloseHandle(proc);
            return res;
        }
        flag_page = reinterpret_cast<uintptr_t>(fp);
        const auto st_emit = [&](const void* p, size_t n) {
            const uint8_t* b = reinterpret_cast<const uint8_t*>(p);
            stub.insert(stub.end(), b, b + n);
        };
        // mov rax, flag
        const uint8_t mvrax[] = { 0x48, 0xB8 };
        st_emit(mvrax, 2); st_emit(&flag_page, 8);
        // lock bts qword ptr [rax], 0
        const uint8_t lockbts[] = { 0xF0, 0x0F, 0xBA, 0x28, 0x00 };
        st_emit(lockbts, sizeof(lockbts));
        // jc +42 (skip the jump block)
        const uint8_t jc[] = { 0x72, (uint8_t)jump_block.size() };
        st_emit(jc, 2);
        // jump block + skip: xor eax,eax; ret
        st_emit(jump_block.data(), jump_block.size());
        const uint8_t skip_ret[] = { 0x31, 0xC0, 0xC3 };
        st_emit(skip_ret, 3);
        LogMap("apc mode: one-shot guard flag at 0x%p\n",
               reinterpret_cast<void*>(flag_page));
    } else {
        stub = jump_block;
    }

    void* stub_mem = VirtualAllocEx(proc, nullptr, stub.size(),
                                    MEM_COMMIT | MEM_RESERVE,
                                    PAGE_EXECUTE_READWRITE);
    if (!stub_mem) {
        res.error = "stub alloc failed";
        CloseHandle(proc);
        return res;
    }
    SIZE_T done = 0;
    if (!WriteProcessMemory(proc, stub_mem, stub.data(), stub.size(), &done)) {
        res.error = "stub write failed";
        CloseHandle(proc);
        return res;
    }

    if (mode == Mode::Apc) {
        // No remote thread: queue the guarded bootstrap as an APC onto every
        // existing Roblox thread. The stub + flag page stay mapped for the rest
        // of the session (APCs may be serviced later by a thread that was not
        // in an alertable wait when we queued).
        const size_t queued = QueueApcsToProcess(proc, reinterpret_cast<uintptr_t>(stub_mem));
        LogMap("apc bootstrap queued to %zu threads (stub stays mapped)\n", queued);
        CloseHandle(proc);
        LogMap("waiting for an alertable thread to run DllMain...\n");
        res.ok = queued > 0;
        res.error = queued ? std::string() : "no threads were APCs to";
        return res;
    }

    LogMap("creating entry thread at 0x%p (stub 0x%p)\n",
           reinterpret_cast<void*>(entry), stub_mem);
    HANDLE thread = CreateRemoteThread(proc, nullptr, 0,
                                       reinterpret_cast<LPTHREAD_START_ROUTINE>(stub_mem),
                                       nullptr, 0, nullptr);
    DWORD exit_code = 0;
    if (thread) {
        if (WaitForSingleObject(thread, 3000) == WAIT_OBJECT_0)
            GetExitCodeThread(thread, &exit_code);
        CloseHandle(thread);
    } else {
        LogMap("CreateRemoteThread failed (last error %lu)\n", GetLastError());
    }
    VirtualFreeEx(proc, stub_mem, 0, MEM_RELEASE);

    if (mode == Mode::ThreadTest) {
        // Environment probe: can a remote thread even LoadLibrary a system DLL?
        // If this ALSO dies with 0xC000071C, the game's anti-cheat is killing
        // foreign threads/loads and our payload never stood a chance.
        wchar_t sysdir[MAX_PATH] = {};
        GetSystemDirectoryW(sysdir, MAX_PATH);
        const std::wstring spath = std::wstring(sysdir) + L"\\shell32.dll";
        const size_t sb = (spath.size() + 1) * sizeof(wchar_t);
        void* sm = VirtualAllocEx(proc, nullptr, sb,
                                  MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (sm) {
            WriteProcessMemory(proc, sm, spath.c_str(), sb, nullptr);
            HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
            FARPROC ll = k32 ? GetProcAddress(k32, "LoadLibraryExW") : nullptr;
            HANDLE lt = ll
                ? CreateRemoteThread(proc, nullptr, 0,
                                     reinterpret_cast<LPTHREAD_START_ROUTINE>(ll),
                                     sm, 0, nullptr)
                : nullptr;
            DWORD lcode = 0;
            if (lt) {
                if (WaitForSingleObject(lt, 8000) == WAIT_OBJECT_0)
                    GetExitCodeThread(lt, &lcode);
                CloseHandle(lt);
                LogMap("thread-test: system-dll LoadLibraryExW probe (shell32.dll) "
                       "-> exit code 0x%X%s\n", lcode,
                       lcode ? "  (BAD - environment interfered)" : "  (clean - remote LoadLibrary works)");
            } else {
                LogMap("thread-test: system-dll LoadLibraryExW probe: CreateRemoteThread "
                       "failed (gerr=%lu)\n", GetLastError());
            }
            VirtualFreeEx(proc, sm, 0, MEM_RELEASE);
        }
    }

    if (crash_page) {
        ReadCrashReport(proc, crash_page);
        VirtualFreeEx(proc, reinterpret_cast<LPVOID>(crash_page), 0, MEM_RELEASE);
    }

    CloseHandle(proc);
    LogMap("entry thread exit code 0x%X - done\n", exit_code);
    res.ok = true;
    return res;
}

} // namespace ManualMapper
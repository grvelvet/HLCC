#include "goldsrc/hook_x86.hpp"

#include <cstring>

namespace hook {

size_t InsnLen32(std::span<const uint8_t> code) {
    if (code.empty()) return 0;
    size_t idx = 0;
    uint8_t op = code[idx];
    size_t len = 1;
    // Skip prefixes (66, 67, F2, F3, 2E, 3E, 64, 65)
    while (op == 0x66 || op == 0x67 || op == 0xF2 || op == 0xF3 ||
           op == 0x2E || op == 0x3E || op == 0x64 || op == 0x65) {
        ++idx;
        if (idx >= code.size()) return 0;
        ++len;
        op = code[idx];
    }
    // Single-byte push/pop/inc/dec/nop/ret
    if ((op >= 0x50 && op <= 0x5F) || op == 0x90 || op == 0xC3 || op == 0xCC) return len;
    // push imm8 (6A xx)
    if (op == 0x6A) return (idx + 1 < code.size()) ? (len + 1) : 0;
    // push imm32 (68 xx xx xx xx), mov reg, imm32 (B8..BF), mov eax,[disp32] (A1/A3)
    if (op == 0x68 || (op >= 0xB8 && op <= 0xBF) || op == 0xA1 || op == 0xA3) {
        return (idx + 4 < code.size()) ? (len + 4) : 0;
    }
    // rel8 jmp/jcc
    if (op == 0xEB || (op >= 0x70 && op <= 0x7F)) return (idx + 1 < code.size()) ? (len + 1) : 0;
    // call/jmp rel32
    if (op == 0xE8 || op == 0xE9) return (idx + 4 < code.size()) ? (len + 4) : 0;

    auto modrm_len = [&](size_t mIdx, size_t imm_bytes) -> size_t {
        if (mIdx >= code.size()) return 0;
        uint8_t mod = (code[mIdx] >> 6) & 3;
        uint8_t rm  = code[mIdx] & 7;
        size_t extra = 1; // ModRM byte
        if (mod != 3 && rm == 4) { // SIB
            if (mIdx + 1 >= code.size()) return 0;
            uint8_t sib = code[mIdx + 1];
            extra++;
            if (mod == 0 && (sib & 7) == 5) extra += 4;
        }
        if (mod == 0 && rm == 5) extra += 4;
        else if (mod == 1) extra += 1;
        else if (mod == 2) extra += 4;
        return (idx + extra + imm_bytes < code.size()) ? (len + extra + imm_bytes) : 0;
    };

    if (op == 0x8B || op == 0x89 || op == 0x8A || op == 0x88 || op == 0x8D ||
        op == 0x33 || op == 0x31 || op == 0x3B || op == 0x39 ||
        op == 0x2B || op == 0x29 || op == 0x03 || op == 0x01 || op == 0x85 || op == 0xFF) {
        return modrm_len(idx + 1, 0);
    }
    if (op == 0x83 || op == 0xC1 || op == 0xC0) return modrm_len(idx + 1, 1);
    if (op == 0x81 || op == 0xC7) return modrm_len(idx + 1, 4);
    if (op == 0x0F) {
        if (idx + 1 >= code.size()) return 0;
        uint8_t op2 = code[idx + 1];
        len++;
        if (op2 >= 0x80 && op2 <= 0x8F) return (idx + 5 < code.size()) ? (len + 4) : 0;
        return modrm_len(idx + 2, 0);
    }
    return 0; // unknown
}

void* InstallInlineHook(void* target, void* detour) {
    if (!target || !detour) return nullptr;
    uint8_t* src = static_cast<uint8_t*>(target);
    std::span<const uint8_t> codeWindow{src, 32};

    size_t stolen = 0;
    while (stolen < 5) {
        size_t ilen = InsnLen32(codeWindow.subspan(stolen));
        if (ilen == 0) return nullptr;
        stolen += ilen;
    }

    uint8_t* tramp = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, stolen + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) return nullptr;

    std::memcpy(tramp, src, stolen);
    // Fixup if first instruction was rel32 call/jmp
    if (stolen == 5 && (src[0] == 0xE8 || src[0] == 0xE9)) {
        int32_t orig_disp = ReadUnaligned<int32_t>(codeWindow.subspan(1, 4));
        uintptr_t orig_target = reinterpret_cast<uintptr_t>(src + 5) + static_cast<uintptr_t>(orig_disp);
        int32_t new_disp = static_cast<int32_t>(orig_target - reinterpret_cast<uintptr_t>(tramp + 5));
        std::memcpy(tramp + 1, &new_disp, 4);
    }

    tramp[stolen] = 0xE9;
    int32_t back_disp = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(src + stolen) - reinterpret_cast<uintptr_t>(tramp + stolen + 5));
    std::memcpy(tramp + stolen + 1, &back_disp, 4);

    DWORD oldProt = 0;
    if (!VirtualProtect(src, stolen, PAGE_EXECUTE_READWRITE, &oldProt)) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        return nullptr;
    }
    src[0] = 0xE9;
    int32_t hook_disp = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(detour) - reinterpret_cast<uintptr_t>(src + 5));
    std::memcpy(src + 1, &hook_disp, 4);
    for (size_t i = 5; i < stolen; ++i) src[i] = 0x90;
    VirtualProtect(src, stolen, oldProt, &oldProt);
    FlushInstructionCache(GetCurrentProcess(), src, stolen);
    return tramp;
}

void* FindFunctionByStringXref(HMODULE mod, std::string_view needle) {
    if (!mod || needle.empty()) return nullptr;
    auto base = reinterpret_cast<const uint8_t*>(mod);
    auto dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto nt   = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    std::span<const IMAGE_SECTION_HEADER> secs{IMAGE_FIRST_SECTION(nt), nt->FileHeader.NumberOfSections};
    const uint8_t* strAddr = nullptr;
    auto needleBytes = std::as_bytes(std::span{needle.data(), needle.size()});

    for (const auto& sec : secs) {
        std::span<const uint8_t> secSpan{base + sec.VirtualAddress, sec.Misc.VirtualSize};
        if (secSpan.size() < needle.size()) continue;
        auto it = std::search(secSpan.begin(), secSpan.end(),
                              reinterpret_cast<const uint8_t*>(needleBytes.data()),
                              reinterpret_cast<const uint8_t*>(needleBytes.data()) + needleBytes.size());
        if (it != secSpan.end()) {
            strAddr = &(*it);
            break;
        }
    }
    if (!strAddr) return nullptr;

    uint32_t targetVA = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(strAddr));
    for (const auto& sec : secs) {
        if (!(sec.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        std::span<const uint8_t> codeSec{base + sec.VirtualAddress, sec.Misc.VirtualSize};
        if (codeSec.size() < 4) continue;
        for (size_t off = 0; off + 4 <= codeSec.size(); ++off) {
            if (ReadUnaligned<uint32_t>(codeSec.subspan(off, 4)) == targetVA) {
                size_t backLimit = (off > 0x400) ? (off - 0x400) : 0;
                for (size_t k = off; k > backLimit; --k) {
                    if (k + 2 < codeSec.size() &&
                        codeSec[k] == 0x55 && codeSec[k + 1] == 0x8B && codeSec[k + 2] == 0xEC) {
                        if (k == 0 || codeSec[k - 1] == 0xCC || codeSec[k - 1] == 0x90 || codeSec[k - 1] == 0xC3) {
                            return const_cast<uint8_t*>(codeSec.data() + k);
                        }
                    }
                }
            }
        }
    }
    return nullptr;
}

} // namespace hook

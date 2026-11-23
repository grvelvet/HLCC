#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace hook {

template <typename T>
inline T ReadUnaligned(std::span<const uint8_t> bytes) {
    std::array<uint8_t, sizeof(T)> buf{};
    std::copy_n(bytes.begin(), sizeof(T), buf.begin());
    return std::bit_cast<T>(buf);
}

size_t InsnLen32(std::span<const uint8_t> code);
void* InstallInlineHook(void* target, void* detour);
void* FindFunctionByStringXref(HMODULE mod, std::string_view needle);

} // namespace hook

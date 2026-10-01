#pragma once

#include <cstddef>
#include <cstdint>

namespace matrix_core {
constexpr std::uint8_t kMaxRows = 6;
// Note: user-editable matrices are limited to 6 columns in the shell UI, but
// some internal computations (e.g. solve via augmented [A|b]) need 7.
constexpr std::uint8_t kMaxCols = 7;
constexpr std::size_t kMaxEntries = static_cast<std::size_t>(kMaxRows) * static_cast<std::size_t>(kMaxCols);
} // namespace matrix_core

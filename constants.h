#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <cstddef>

namespace constants {
inline constexpr size_t MSG_LEN_BYTES{4};
inline constexpr size_t MAX_MSG_SIZE{4096};
inline constexpr size_t BUFFER_SIZE{MSG_LEN_BYTES + MAX_MSG_SIZE};
inline constexpr int TIMEOUT{-1};
} // namespace constants

#endif
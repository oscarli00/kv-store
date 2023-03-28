#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <cstddef>

namespace constants {
inline constexpr size_t MSG_LEN_BYTES{4};
inline constexpr size_t MAX_MSG_SIZE{4096};
inline constexpr size_t READ_BUFFER_SIZE{MSG_LEN_BYTES + MAX_MSG_SIZE};
inline constexpr size_t WRITE_BUFFER_SIZE{MSG_LEN_BYTES + MAX_MSG_SIZE};
inline constexpr size_t EVENT_BUFFER_SIZE{16 * 1024};
inline constexpr int TIMEOUT{-1};
} // namespace constants

#endif
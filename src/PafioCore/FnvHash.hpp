#pragma once

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace pafio
{

// FNV-1a 64-bit hash shared by git source cache naming and compile-plan keys.
inline uint64_t
Fnv1a64(const std::string &value)
{
  uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char ch : value)
  {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  return hash;
}

inline std::string
Hex64(uint64_t value)
{
  std::ostringstream out;
  out << std::hex << value;
  return out.str();
}

}  // namespace pafio

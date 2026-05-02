#include "SpioCore/Sha256.hpp"

#include "SpioCore/Errors.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace
{

class Sha256
{
public:
  void Update(const uint8_t *data, const size_t size)
  {
    total_size_ += static_cast<uint64_t>(size);
    size_t offset = 0;
    while (offset < size)
    {
      const size_t chunk = std::min<size_t>(buffer_size_ < 64 ? 64 - buffer_size_ : 0, size - offset);
      std::copy_n(data + offset, chunk, buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_));
      buffer_size_ += chunk;
      offset += chunk;
      if (buffer_size_ == 64)
      {
        Transform(buffer_.data());
        buffer_size_ = 0;
      }
    }
  }

  std::string FinalHex()
  {
    std::array<uint8_t, 64> final_block = buffer_;
    size_t final_size = buffer_size_;
    final_block[final_size++] = 0x80;

    if (final_size > 56)
    {
      std::fill(final_block.begin() + static_cast<std::ptrdiff_t>(final_size), final_block.end(), 0U);
      Transform(final_block.data());
      final_block.fill(0U);
      final_size = 0;
    }

    std::fill(final_block.begin() + static_cast<std::ptrdiff_t>(final_size), final_block.begin() + 56, 0U);
    const uint64_t total_bits = total_size_ * 8U;
    for (size_t index = 0; index < 8; ++index)
    {
      final_block[63 - static_cast<std::ptrdiff_t>(index)] = static_cast<uint8_t>((total_bits >> (index * 8U)) & 0xffU);
    }
    Transform(final_block.data());

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const uint32_t word : state_)
    {
      out << std::setw(8) << word;
    }
    return out.str();
  }

private:
  static constexpr std::array<uint32_t, 64> kConstants = {
      0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
      0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
      0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
      0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
      0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
      0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
      0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
      0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
  };

  static uint32_t RotateRight(const uint32_t value, const uint32_t bits)
  {
    return (value >> bits) | (value << (32U - bits));
  }

  static uint32_t Ch(const uint32_t x, const uint32_t y, const uint32_t z)
  {
    return (x & y) ^ (~x & z);
  }

  static uint32_t Maj(const uint32_t x, const uint32_t y, const uint32_t z)
  {
    return (x & y) ^ (x & z) ^ (y & z);
  }

  static uint32_t BigSigma0(const uint32_t value)
  {
    return RotateRight(value, 2U) ^ RotateRight(value, 13U) ^ RotateRight(value, 22U);
  }

  static uint32_t BigSigma1(const uint32_t value)
  {
    return RotateRight(value, 6U) ^ RotateRight(value, 11U) ^ RotateRight(value, 25U);
  }

  static uint32_t SmallSigma0(const uint32_t value)
  {
    return RotateRight(value, 7U) ^ RotateRight(value, 18U) ^ (value >> 3U);
  }

  static uint32_t SmallSigma1(const uint32_t value)
  {
    return RotateRight(value, 17U) ^ RotateRight(value, 19U) ^ (value >> 10U);
  }

  void Transform(const uint8_t *chunk)
  {
    std::array<uint32_t, 64> schedule{};
    for (size_t index = 0; index < 16; ++index)
    {
      const size_t base = index * 4U;
      schedule[index] = (static_cast<uint32_t>(chunk[base]) << 24U) |
                        (static_cast<uint32_t>(chunk[base + 1U]) << 16U) |
                        (static_cast<uint32_t>(chunk[base + 2U]) << 8U) |
                        static_cast<uint32_t>(chunk[base + 3U]);
    }
    for (size_t index = 16; index < schedule.size(); ++index)
    {
      schedule[index] = SmallSigma1(schedule[index - 2U]) + schedule[index - 7U] + SmallSigma0(schedule[index - 15U]) +
                        schedule[index - 16U];
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];

    for (size_t index = 0; index < 64; ++index)
    {
      const uint32_t t1 = h + BigSigma1(e) + Ch(e, f, g) + kConstants[index] + schedule[index];
      const uint32_t t2 = BigSigma0(a) + Maj(a, b, c);
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<uint32_t, 8> state_ = {
      0x6a09e667U,
      0xbb67ae85U,
      0x3c6ef372U,
      0xa54ff53aU,
      0x510e527fU,
      0x9b05688cU,
      0x1f83d9abU,
      0x5be0cd19U,
  };
  std::array<uint8_t, 64> buffer_{};
  size_t buffer_size_ = 0;
  uint64_t total_size_ = 0;
};

}  // namespace

namespace spio
{

std::string Sha256File(const fs::path &path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
  {
    throw CacheError("failed to open file for sha256: " + path.string());
  }

  Sha256 sha256;
  std::array<char, 8192> buffer{};
  while (in.good())
  {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = in.gcount();
    if (count > 0)
    {
      sha256.Update(reinterpret_cast<const uint8_t *>(buffer.data()), static_cast<size_t>(count));
    }
  }
  if (!in.eof())
  {
    throw CacheError("failed while reading file for sha256: " + path.string());
  }
  return sha256.FinalHex();
}

std::string Sha256Text(const std::string &text)
{
  Sha256 sha256;
  sha256.Update(reinterpret_cast<const uint8_t *>(text.data()), text.size());
  return sha256.FinalHex();
}

}  // namespace spio

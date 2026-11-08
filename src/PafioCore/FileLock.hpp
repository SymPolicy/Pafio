#pragma once

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace pafio
{

class LockHeldError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

enum class FileLockScope
{
  kProject,
  kCache,
  kTrust,
};

// Advisory per-store lock. POSIX flock now; Windows mapping deferred (REQ-PLAT-001).
// Lock order: project -> cache -> trust.
class FileLockGuard
{
public:
  FileLockGuard() = default;
  FileLockGuard(const FileLockGuard &) = delete;
  FileLockGuard &operator=(const FileLockGuard &) = delete;
  FileLockGuard(FileLockGuard &&other) noexcept;
  FileLockGuard &operator=(FileLockGuard &&other) noexcept;
  ~FileLockGuard();

  explicit operator bool() const {
    return fd_ >= 0;
  }

private:
  friend FileLockGuard AcquireFileLock(
    const std::filesystem::path &store_path,
    FileLockScope scope,
    std::chrono::milliseconds timeout
  );

  void Release() noexcept;

  int fd_ = -1;
  std::filesystem::path lock_path_;
};

FileLockGuard AcquireFileLock(
  const std::filesystem::path &store_path,
  FileLockScope scope,
  std::chrono::milliseconds timeout = std::chrono::seconds{30}
);

}  // namespace pafio

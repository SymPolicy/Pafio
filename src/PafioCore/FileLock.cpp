#include "PafioCore/FileLock.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#include "PafioCore/Errors.hpp"

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace
{

std::filesystem::path
LockFileFor(const std::filesystem::path &store_path, pafio::FileLockScope scope) {
  const char *suffix = "project";
  switch (scope) {
    case pafio::FileLockScope::kProject:
      suffix = "project";
      break;
    case pafio::FileLockScope::kCache:
      suffix = "cache";
      break;
    case pafio::FileLockScope::kTrust:
      suffix = "trust";
      break;
  }
  return store_path / (std::string(".pafio-") + suffix + ".lock");
}

}  // namespace

namespace pafio
{

FileLockGuard::FileLockGuard(FileLockGuard &&other) noexcept
    :
    fd_(other.fd_), lock_path_(std::move(other.lock_path_)) {
  other.fd_ = -1;
}

FileLockGuard &
FileLockGuard::operator=(FileLockGuard &&other) noexcept {
  if (this != &other) {
    Release();
    fd_ = other.fd_;
    lock_path_ = std::move(other.lock_path_);
    other.fd_ = -1;
  }
  return *this;
}

FileLockGuard::~FileLockGuard() {
  Release();
}

void
FileLockGuard::Release() noexcept {
#if !defined(_WIN32)
  if (fd_ >= 0) {
    ::flock(fd_, LOCK_UN);
    ::close(fd_);
    fd_ = -1;
  }
#else
  fd_ = -1;
#endif
  lock_path_.clear();
}

FileLockGuard
AcquireFileLock(
  const std::filesystem::path &store_path,
  FileLockScope scope,
  std::chrono::milliseconds timeout
) {
#if defined(_WIN32)
  (void)timeout;
  // Advisory flock is POSIX-only; Windows mapping is deferred (REQ-PLAT-001).
  // Return an unlocked guard so AtomicFile callers still function in limited Windows builds.
  std::filesystem::create_directories(store_path);
  FileLockGuard guard;
  guard.lock_path_ = LockFileFor(store_path, scope);
  return guard;
#else
  std::filesystem::create_directories(store_path);
  if (timeout < std::chrono::milliseconds::zero()) {
    throw CacheError("file lock timeout must not be negative");
  }
  FileLockGuard guard;
  guard.lock_path_ = LockFileFor(store_path, scope);
  guard.fd_ = ::open(guard.lock_path_.c_str(), O_RDWR | O_CREAT, 0644);
  if (guard.fd_ < 0) {
    throw CacheError("failed to open lock file: " + guard.lock_path_.string());
  }

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (true) {
    if (::flock(guard.fd_, LOCK_EX | LOCK_NB) == 0) {
      return guard;
    }
    const int lock_error = errno;
    if (lock_error != EWOULDBLOCK && lock_error != EAGAIN && lock_error != EINTR) {
      throw CacheError(
        "failed to acquire lock file '" + guard.lock_path_.string() + "': " + std::strerror(lock_error)
      );
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      ::close(guard.fd_);
      guard.fd_ = -1;
      throw LockHeldError("lock held for store: " + guard.lock_path_.string());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
  }
#endif
}

}  // namespace pafio

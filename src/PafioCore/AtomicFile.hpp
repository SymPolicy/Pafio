#pragma once

#include <filesystem>
#include <string_view>

namespace pafio
{

// An exclusively created temporary file beside a target path. Creation is
// atomic — mkstemp on POSIX, CreateFile CREATE_NEW on Windows — so a
// pre-planted file or symlink at a predictable name cannot redirect the bytes
// written before the final rename over the target.
struct ExclusiveTempFile
{
  std::filesystem::path path;
#if defined(_WIN32)
  void *handle = nullptr;
#else
  int fd = -1;
#endif
};

// Opens a sibling temporary file for `target` with an exclusive-create
// primitive, retrying collisions with a fresh suffix. Throws
// std::system_error on failure.
ExclusiveTempFile OpenExclusiveTemporaryFile(const std::filesystem::path &target);
// Creates `candidate` only if it does not already exist; returns false when
// the name is taken and throws std::system_error for other creation
// failures. The testable exclusive-create primitive behind
// OpenExclusiveTemporaryFile.
bool TryCreateExclusiveFile(const std::filesystem::path &candidate, ExclusiveTempFile &temp);
void WriteTemporaryFileFully(ExclusiveTempFile &temp, const char *data, size_t size);
void FlushTemporaryFile(ExclusiveTempFile &temp);
// Checked close for the success path — a close-time I/O error must not be
// reported as a finalized write. Throws std::system_error on failure.
void CloseTemporaryFile(ExclusiveTempFile &temp);
// Best-effort close for exception cleanup.
void DiscardTemporaryFile(ExclusiveTempFile &temp) noexcept;

// Same-directory temp + fsync + rename for mutable state files.
void AtomicWriteFile(const std::filesystem::path &path, std::string_view content);

}  // namespace pafio

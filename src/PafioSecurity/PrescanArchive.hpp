#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pafio
{

struct ArchivePrescanResult
{
  bool ok = false;
  std::string error;
  std::string error_code;
  size_t entry_count = 0;
  size_t manifest_candidates = 0;
};

// Pure listing prescan: reject absolute/drive/.. paths and non-file/dir member types.
// Fail closed when listing_truncated is true (stdout overflow).
ArchivePrescanResult PrescanArchiveListing(
    std::string_view path_listing_text,
    std::string_view verbose_listing_text,
    bool listing_truncated,
    bool require_single_pafio_toml);

// Bound used by callers for tar -tf/-tvf capture. Overflow must fail closed.
inline constexpr size_t kArchiveListingMaxBytes = 64U * 1024U * 1024U;

}  // namespace pafio

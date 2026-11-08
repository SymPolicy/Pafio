#include "PafioSecurity/PrescanArchive.hpp"

#include "PafioCore/Errors.hpp"
#include "PafioSecurity/RegistrySecurity.hpp"

#include <sstream>
#include <string>

namespace
{

void Fail(pafio::ArchivePrescanResult &result, std::string code, std::string message)
{
  result.ok = false;
  result.error_code = std::move(code);
  result.error = std::move(message);
}

bool LooksAbsoluteOrDrive(const std::string &path)
{
  if (path.empty())
  {
    return true;
  }
  if (path.front() == '/' || path.front() == '\\')
  {
    return true;
  }
  if (path.size() >= 2 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':')
  {
    return true;
  }
  return false;
}

}  // namespace

namespace pafio
{

ArchivePrescanResult PrescanArchiveListing(
    std::string_view path_listing_text,
    std::string_view verbose_listing_text,
    bool listing_truncated,
    bool require_single_pafio_toml)
{
  ArchivePrescanResult result;
  if (listing_truncated)
  {
    Fail(result, "prescan.listing_overflow", "archive listing exceeded capture limit; refusing extraction");
    return result;
  }

  std::istringstream path_lines{std::string(path_listing_text)};
  std::string entry_path;
  while (std::getline(path_lines, entry_path))
  {
    const std::string original_entry_path = entry_path;
    while (!entry_path.empty() && (entry_path.back() == '/' || entry_path.back() == '\\'))
    {
      entry_path.pop_back();
    }
    if (entry_path.empty())
    {
      Fail(result, "prescan.empty_path", "archive member path is empty after normalization: " + original_entry_path);
      return result;
    }
    if (LooksAbsoluteOrDrive(entry_path))
    {
      Fail(result, "prescan.absolute_path", "archive member path must be relative: " + original_entry_path);
      return result;
    }
    try
    {
      (void) NormalizeRegistryObjectPath(entry_path, "archive member path");
    }
    catch (const FetchError &error)
    {
      Fail(result, "prescan.traversal", error.what());
      return result;
    }
    ++result.entry_count;
    if (entry_path == "pafio.toml" || entry_path.ends_with("/pafio.toml"))
    {
      ++result.manifest_candidates;
    }
  }

  if (require_single_pafio_toml && result.manifest_candidates != 1U)
  {
    Fail(result, "prescan.manifest", "archive must contain exactly one pafio.toml manifest");
    return result;
  }

  std::istringstream verbose_lines{std::string(verbose_listing_text)};
  std::string verbose_line;
  while (std::getline(verbose_lines, verbose_line))
  {
    if (verbose_line.empty())
    {
      continue;
    }
    const char type = verbose_line.front();
    if (type != '-' && type != 'd')
    {
      Fail(result, "prescan.member_type", "archive member type is not allowed: " + verbose_line);
      return result;
    }
  }

  result.ok = true;
  return result;
}

}  // namespace pafio

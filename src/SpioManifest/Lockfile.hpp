#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace spio
{

struct LockPackage
{
  std::string id;
  std::string name;
  std::string version;
  std::string source_kind;
  std::optional<std::string> git;
  std::optional<std::string> rev;
  std::vector<std::string> dependencies;
};

struct LockfileDocument
{
  std::string generated_by;
  std::string resolver;
  std::vector<LockPackage> packages;
};

LockfileDocument LoadLockfile(const std::filesystem::path &lockfile_path);
std::string SerializeLockfileCanonical(const LockfileDocument &lockfile);

}  // namespace spio

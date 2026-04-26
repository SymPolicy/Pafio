#pragma once

#include "SpioToolchain/Vocabulary.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace spio
{

struct SourceBuildRequest
{
  std::filesystem::path manifest_path = "spio.toml";
  std::string channel = std::string(kChannelStable);
  std::string build_mode = std::string(kBuildModeMinimal);
  std::optional<std::filesystem::path> explicit_source_root;
  std::optional<std::string> source_revision;
  bool allow_fetch = true;
  bool offline = false;
  bool assume_yes = false;
  bool non_interactive = false;
};

struct SourceBuildResult
{
  std::filesystem::path source_root;
  std::filesystem::path compiler_binary;
  std::string source_revision;
  std::string channel;
  std::string build_mode;
  bool fetched = false;
  bool built = false;
};

SourceBuildResult EnsureSourceBuiltStyio(const SourceBuildRequest &request);

}  // namespace spio

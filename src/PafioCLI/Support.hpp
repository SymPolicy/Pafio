#pragma once

#include "PafioCore/Errors.hpp"
#include "PafioPlan/CompilePlan.hpp"
#include "PafioResolve/Resolver.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace pafio
{

struct WorkflowFlags
{
  bool locked = false;
  bool offline = false;
};

struct SourceWorkflowFlags
{
  bool allow_fetch = true;
  bool assume_yes = false;
  bool non_interactive = false;
  std::optional<std::filesystem::path> source_root;
  std::optional<std::string> source_revision;
};

struct ParsedPlanInvocation
{
  BuildPlanRequest request;
  std::optional<std::string> styio_bin;
  bool dry_run = false;
  WorkflowFlags workflow_flags;
  SourceWorkflowFlags source_flags;
};

int EmitError(const CommandError &error, bool as_json);
int EmitSuccess(const nlohmann::json &payload, bool as_json);
int EmitBootstrapNotImplemented(std::string_view command, bool as_json);
int PrintGlobalHelp();
int PrintCommandUsage(std::string_view command);

std::string TrimAsciiWhitespace(std::string value);
std::optional<std::string> NormalizeRegistryHeader(std::string value);
std::string KindFromFlags(const std::vector<std::string> &args, size_t &index);
std::string ReadFile(const std::filesystem::path &path);
bool IsHttpRegistryRoot(const std::string &value);
bool IsFileRegistryRoot(const std::string &value);
std::filesystem::path FileRegistryUrlToPath(const std::string &value);
bool ConsumeWorkflowFlag(const std::string &argument, WorkflowFlags &flags);
bool ConsumeSourceWorkflowFlag(
    std::string_view command_name,
    const std::vector<std::string> &args,
    size_t &index,
    SourceWorkflowFlags &flags);
ResolveOptions BuildResolveOptions(
    const std::filesystem::path &manifest_path,
    const WorkflowFlags &flags,
    const std::optional<std::filesystem::path> &vendor_root_override = std::nullopt);
std::string NormalizeSetKeyword(std::string value);
std::optional<CommandError> ParsePlanInvocation(
    std::string_view command_name,
    std::string_view intent,
    bool allow_lib,
    bool allow_bin,
    bool allow_test,
    const std::vector<std::string> &args,
    ParsedPlanInvocation &parsed);
std::optional<CommandError> ValidateLockedPolicy(
    const std::filesystem::path &manifest_path,
    std::string_view command,
    const WorkflowFlags &flags,
    const ResolveOptions &resolve_options);

}  // namespace pafio

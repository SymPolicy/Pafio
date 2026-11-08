#include "PafioApp/PackageApp.hpp"

#include "PafioCLI/Support.hpp"
#include "PafioCore/ContractLexicon.hpp"
#include "PafioCore/Paths.hpp"
#include "PafioCore/Process.hpp"
#include "PafioCore/ToolPaths.hpp"
#include "PafioManifest/Lockfile.hpp"
#include "PafioManifest/Manifest.hpp"
#include "PafioPack/Pack.hpp"
#include "PafioPublish/Publish.hpp"
#include "PafioResolve/ResolutionContract.hpp"
#include "PafioResolve/Resolver.hpp"
#include "PafioSecurity/RegistrySecurity.hpp"
#include "PafioSecurity/RegistryTrust.hpp"
#include "PafioTree/Render.hpp"
#include "PafioVendor/Vendor.hpp"
#include "PafioWorkflow/Dependencies.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

bool EndsWith(std::string_view value, std::string_view suffix)
{
  return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

json ParseJsonObjectText(const std::string &text, const std::string &context)
{
  try
  {
    json payload = json::parse(text);
    if (!payload.is_object())
    {
      throw pafio::PublishError(context + " must emit a top-level JSON object");
    }
    return payload;
  }
  catch (const json::parse_error &)
  {
    throw pafio::PublishError(context + " did not emit valid JSON");
  }
}

std::string ProcessFailureText(const pafio::ProcessResult &result)
{
  return pafio::DescribeProcessFailure(result);
}

std::string RegistryControlPlaneBaseUrl(std::string registry_root)
{
  constexpr std::string_view kControlPlaneBase = "/api/pafio-registry-control/v1";
  registry_root = pafio::NormalizeRegistryRoot(std::move(registry_root));
  if (EndsWith(registry_root, kControlPlaneBase))
  {
    return registry_root;
  }
  return registry_root + std::string(kControlPlaneBase);
}

json RunRegistryV2ControlPlanePublish(
    const std::string &registry_root,
    const std::vector<std::string> &request_headers,
    const pafio::PublishResult &candidate)
{
  const std::string control_plane_base = RegistryControlPlaneBaseUrl(registry_root);
  const std::string request_body = pafio::BuildRemotePublishRequestJson(candidate);

  // -q must be the first curl argument: it disables the user's curl
  // configuration, which could otherwise enable redirects and steer the
  // request body and provider headers to a different host. --max-redirs 0
  // keeps redirect following off even if another flag re-enables it.
  // The transfer bound mirrors the external-process step budget, and the
  // connect bound keeps a dead endpoint from consuming the whole transfer.
  constexpr std::string_view kPublishConnectTimeoutSeconds{"10"};
  const std::string publish_max_time_seconds = std::to_string(
      std::chrono::duration_cast<std::chrono::seconds>(pafio::kExternalProcessStepTimeout).count());
  const std::string scheme = registry_root.substr(0, registry_root.find("://"));
  std::vector<std::string> args = {
      "-q",
      "-fsS",
      "--connect-timeout",
      std::string(kPublishConnectTimeoutSeconds),
      "--max-time",
      publish_max_time_seconds,
      "--max-redirs",
      "0",
      "--proto",
      "=" + scheme,
      "-X",
      "POST",
      control_plane_base + "/publish",
      "-H",
      "Content-Type: application/json",
  };
  for (const std::string &header : request_headers)
  {
    args.push_back("-H");
    args.push_back(header);
  }
  args.push_back("--data-binary");
  args.push_back("@-");

  const pafio::ProcessResult result = pafio::RunProcess<pafio::PublishError>({
      .program = pafio::ResolvedCurlPath(),
      .args = args,
      .search_path = false,
      .timeout = pafio::kExternalProcessStepTimeout,
      .stdin_text = request_body,
      .error_context = "registry control-plane publish request",
  });
  if (result.exit_code != 0)
  {
    throw pafio::PublishError("registry control-plane publish request failed: " + ProcessFailureText(result));
  }

  const json envelope = ParseJsonObjectText(result.stdout_text, "registry control-plane publish response");
  const int returncode = envelope.value("returncode", -1);
  if (returncode != 0)
  {
    std::string detail;
    if (envelope.contains("error_payload") && envelope["error_payload"].is_object())
    {
      detail = envelope["error_payload"].value("detail", "");
    }
    if (detail.empty())
    {
      detail = envelope.value("stderr", "");
    }
    if (detail.empty())
    {
      detail = envelope.value("message", "registry control-plane returned a failure envelope");
    }
    throw pafio::PublishError("registry control-plane publish failed: " + detail);
  }
  if (!envelope.contains("payload") || !envelope["payload"].is_object())
  {
    throw pafio::PublishError("registry control-plane publish response is missing payload");
  }
  return envelope["payload"];
}

json BuildHttpPublishPayload(
    const pafio::PublishResult &candidate,
    const pafio::RegistryWriteSecurityDecision &security,
    const json &publish_result)
{
  json payload = {
      {"command", "publish"},
      {"mode", "publish"},
      {"transport", "http"},
      {"registry_protocol", "pafio-static-registry"},
      {"message", "published package through the Pafio registry control plane"},
      {"package", candidate.package_name},
      {"version", candidate.package_version},
      {"dependencies", candidate.dependency_count},
      {"dev_dependencies", candidate.dev_dependency_count},
      {"registry_index_path", publish_result.at("index_path")},
      {"registry_artifact_path", publish_result.at("artifact_path")},
      {"registry_log_leaf_path", publish_result.at("log_leaf_path")},
      {"created_root", publish_result.value("created_root", false)},
      {"sequence", publish_result.value("sequence", 0)},
      {"checkpoint_version", publish_result.value("checkpoint_version", 0)},
      {"snapshot_version", publish_result.value("snapshot_version", 0)},
      {"timestamp_version", publish_result.value("timestamp_version", 0)},
      {"registry_security_provider", security.provider_name},
      {"registry_write_security_mode", security.mode},
      {"registry_header_count", security.request_headers.size()},
      {"sha256", publish_result.at("archive_sha256")},
      {"size_bytes", publish_result.at("archive_size_bytes")},
      {"published_at", publish_result.at("published_at")},
  };
  return payload;
}

}  // namespace

namespace pafio
{

int HandleNew(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("new");
  }
  if (args.empty())
  {
    return EmitError({"UsageError", kExitUsage, "new requires a package name", "new"}, as_json);
  }

  size_t index = 0;
  const std::string name = args[index++];
  fs::path directory;
  if (index < args.size() && !args[index].starts_with("--"))
  {
    directory = fs::path(args[index++]);
  }
  else
  {
    const auto slash = name.find('/');
    directory = slash == std::string::npos ? fs::path(name) : fs::path(name.substr(slash + 1));
  }

  const std::string kind = KindFromFlags(args, index);
  if (index != args.size())
  {
    return EmitError({"UsageError", kExitUsage, "unexpected arguments for new", "new"}, as_json);
  }

  try
  {
    InitializeProject({.package_name = name, .root = directory, .kind = kind});
  }
  catch (const std::exception &err)
  {
    return EmitError({"UsageError", kExitUsage, err.what(), "new"}, as_json);
  }

  return EmitSuccess(
      {
          {"command", "new"},
          {"message", "initialized project at " + directory.string()},
          {"root", fs::absolute(directory).string()},
      },
      as_json);
}

int HandleInit(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("init");
  }
  std::optional<std::string> name;
  size_t index = 0;
  while (index < args.size())
  {
    if (args[index] == "--name")
    {
      ++index;
      if (index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--name requires a value", "init"}, as_json);
      }
      name = args[index++];
      continue;
    }
    break;
  }

  const std::string kind = KindFromFlags(args, index);
  if (index != args.size())
  {
    return EmitError({"UsageError", kExitUsage, "unexpected arguments for init", "init"}, as_json);
  }

  const fs::path root = fs::current_path();
  const std::string package_name = name.value_or(InferLocalPackageName(root));
  try
  {
    InitializeProject({.package_name = package_name, .root = root, .kind = kind});
  }
  catch (const std::exception &err)
  {
    return EmitError({"UsageError", kExitUsage, err.what(), "init"}, as_json);
  }

  return EmitSuccess(
      {
          {"command", "init"},
          {"message", "initialized project in " + root.string()},
          {"root", fs::absolute(root).string()},
          {"package", package_name},
      },
      as_json);
}

int HandleAdd(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("add");
  }
  if (args.empty())
  {
    return EmitError({"UsageError", kExitUsage, "add requires a package name", "add"}, as_json);
  }

  AddDependencyRequest request{
      .manifest_path = "pafio.toml",
      .package_name = args.front(),
  };
  bool saw_source = false;
  for (size_t index = 1; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "add"}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--alias")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--alias requires a value", "add"}, as_json);
      }
      request.alias = args[index];
    }
    else if (args[index] == "--dev")
    {
      request.section = DependencySection::kDevDependencies;
    }
    else if (args[index] == "--path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--path requires a value", "add"}, as_json);
      }
      if (saw_source)
      {
        return EmitError({"UsageError", kExitUsage, "add accepts exactly one dependency source", "add"}, as_json);
      }
      request.use_git = false;
      request.source = args[index];
      saw_source = true;
    }
    else if (args[index] == "--git")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--git requires a value", "add"}, as_json);
      }
      if (saw_source)
      {
        return EmitError({"UsageError", kExitUsage, "add accepts exactly one dependency source", "add"}, as_json);
      }
      request.use_git = true;
      request.source = args[index];
      saw_source = true;
    }
    else if (args[index] == "--registry")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--registry requires a value", "add"}, as_json);
      }
      if (saw_source)
      {
        return EmitError({"UsageError", kExitUsage, "add accepts exactly one dependency source", "add"}, as_json);
      }
      request.use_registry = true;
      request.source = args[index];
      saw_source = true;
    }
    else if (args[index] == "--rev")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--rev requires a value", "add"}, as_json);
      }
      request.rev = args[index];
    }
    else if (args[index] == "--version")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--version requires a value", "add"}, as_json);
      }
      request.version = args[index];
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for add: " + args[index], "add"}, as_json);
    }
  }

  if (!saw_source)
  {
    return EmitError(
        {"UsageError", kExitUsage, "add requires --path <path>, --git <source> --rev <rev>, or --registry <url> --version <x.y.z>", "add"},
        as_json);
  }

  try
  {
    const DependencyCommandResult result = AddDependencyAndRefreshLock(request);
    return EmitSuccess(
        {
            {"command", "add"},
            {"message", "added dependency '" + result.alias + "' and refreshed lockfile"},
            {"manifest_path", result.manifest_path.string()},
            {"lockfile_path", result.lockfile_path.string()},
            {"alias", result.alias},
            {"package", result.package_name},
            {"section", DependencySectionName(result.section)},
            {"packages", result.package_count},
            {"source_kind", request.use_git ? "git" : (request.use_registry ? "registry" : "path")},
        },
        as_json);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "add"}, as_json);
  }
  catch (const ResolutionError &err)
  {
    return EmitError({"ResolutionError", kExitResolve, err.what(), "add"}, as_json);
  }
  catch (const FetchError &err)
  {
    return EmitError({"FetchError", kExitFetch, err.what(), "add"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "add"}, as_json);
  }
}

int HandleRemove(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("remove");
  }
  if (args.empty())
  {
    return EmitError({"UsageError", kExitUsage, "remove requires a dependency alias or package name", "remove"}, as_json);
  }

  RemoveDependencyRequest request{
      .manifest_path = "pafio.toml",
      .target = args.front(),
  };
  for (size_t index = 1; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "remove"}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--dev")
    {
      request.section = DependencySection::kDevDependencies;
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for remove: " + args[index], "remove"}, as_json);
    }
  }

  try
  {
    const DependencyCommandResult result = RemoveDependencyAndRefreshLock(request);
    return EmitSuccess(
        {
            {"command", "remove"},
            {"message", "removed dependency '" + result.alias + "' and refreshed lockfile"},
            {"manifest_path", result.manifest_path.string()},
            {"lockfile_path", result.lockfile_path.string()},
            {"alias", result.alias},
            {"package", result.package_name},
            {"section", DependencySectionName(result.section)},
            {"packages", result.package_count},
        },
        as_json);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "remove"}, as_json);
  }
  catch (const ResolutionError &err)
  {
    return EmitError({"ResolutionError", kExitResolve, err.what(), "remove"}, as_json);
  }
  catch (const FetchError &err)
  {
    return EmitError({"FetchError", kExitFetch, err.what(), "remove"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "remove"}, as_json);
  }
}

int HandleSync(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("sync");
  }

  fs::path manifest_path = "pafio.toml";
  WorkflowFlags workflow_flags;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "sync"}, as_json);
      }
      manifest_path = args[index];
    }
    else if (ConsumeWorkflowFlag(args[index], workflow_flags))
    {
      continue;
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for sync: " + args[index], "sync"}, as_json);
    }
  }

  try
  {
    const SyncProjectResult result = SyncProjectDependencies(
        manifest_path,
        BuildResolveOptions(manifest_path, workflow_flags));
    return EmitSuccess(
        {
            {"command", "sync"},
            {"message", "synced project dependencies for " + std::to_string(result.package_count) + " package(s)"},
            {"manifest_path", result.manifest_path.string()},
            {"lockfile_path", result.graph.lockfile_path.string()},
            {"resolution_path", result.resolution_path.string()},
            {"lockfile_mode", result.lockfile_mode},
            {"packages", result.package_count},
            {"git_packages", result.git_package_count},
            {"registry_packages", result.registry_package_count},
            {"locked", workflow_flags.locked},
            {"offline", workflow_flags.offline},
        },
        as_json);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "sync"}, as_json);
  }
  catch (const WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", kExitWorkspace, err.what(), "sync"}, as_json);
  }
  catch (const ResolutionError &err)
  {
    return EmitError({"ResolutionError", kExitResolve, err.what(), "sync"}, as_json);
  }
  catch (const FetchError &err)
  {
    return EmitError({"FetchError", kExitFetch, err.what(), "sync"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "sync"}, as_json);
  }
  catch (const SyncError &err)
  {
    return EmitError({"LockfileError", kExitLock, err.what(), "sync"}, as_json);
  }
}

int HandleTree(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("tree");
  }

  fs::path manifest_path = "pafio.toml";
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "tree"}, as_json);
      }
      manifest_path = args[index];
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for tree: " + args[index], "tree"}, as_json);
    }
  }

  if (!fs::exists(manifest_path))
  {
    return EmitError({"ManifestError", kExitManifest, "manifest not found: " + manifest_path.string(), "tree"}, as_json);
  }

  LockGenerationResult graph;
  try
  {
    graph = ResolveSingleVersionLockfile(manifest_path);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "tree"}, as_json);
  }
  catch (const WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", kExitWorkspace, err.what(), "tree"}, as_json);
  }
  catch (const ResolutionError &err)
  {
    return EmitError({"ResolutionError", kExitResolve, err.what(), "tree"}, as_json);
  }
  catch (const FetchError &err)
  {
    return EmitError({"FetchError", kExitFetch, err.what(), "tree"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "tree"}, as_json);
  }

  if (as_json)
  {
    json packages = json::array();
    for (const LockPackage &package : graph.lockfile.packages)
    {
      json item{
          {"id", package.id},
          {"name", package.name},
          {"version", package.version},
          {"source_kind", package.source_kind},
          {"dependencies", package.dependencies},
      };
      if (package.git.has_value())
      {
        item["git"] = *package.git;
      }
      if (package.rev.has_value())
      {
        item["rev"] = *package.rev;
      }
      if (package.registry.has_value())
      {
        item["registry"] = *package.registry;
      }
      if (package.sha256.has_value())
      {
        item["sha256"] = *package.sha256;
      }
      packages.push_back(std::move(item));
    }

    std::cout << json({
                     {"command", "tree"},
                     {"manifest_path", graph.manifest_path.string()},
                     {"lockfile_path", graph.lockfile_path.string()},
                     {"resolver", graph.lockfile.resolver},
                     {"root_ids", graph.root_ids},
                     {"packages", packages},
                 })
                     .dump()
              << '\n';
    return kExitSuccess;
  }

  std::cout << RenderDependencyTreeText(graph);
  return kExitSuccess;
}

int HandleVendor(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("vendor");
  }

  VendorRequest request;
  WorkflowFlags workflow_flags;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "vendor"}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--output")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--output requires a value", "vendor"}, as_json);
      }
      request.output_path = fs::path(args[index]);
    }
    else if (ConsumeWorkflowFlag(args[index], workflow_flags))
    {
      continue;
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for vendor: " + args[index], "vendor"}, as_json);
    }
  }

  const ResolveOptions resolve_options = BuildResolveOptions(request.manifest_path, workflow_flags, request.output_path);
  if (const auto lock_policy_error = ValidateLockedPolicy(request.manifest_path, "vendor", workflow_flags, resolve_options);
      lock_policy_error.has_value())
  {
    return EmitError(*lock_policy_error, as_json);
  }
  request.offline = workflow_flags.offline;

  try
  {
    const VendorResult result = WriteVendorTree(request);
    return EmitSuccess(
        {
            {"command", "vendor"},
            {"message", "materialized vendored dependency snapshots under " + result.vendor_root.string()},
            {"manifest_path", result.manifest_path.string()},
            {"vendor_root", result.vendor_root.string()},
            {"metadata_path", result.metadata_path.string()},
            {"packages", result.package_count},
            {"git_snapshots", result.git_snapshot_count},
            {"locked", workflow_flags.locked},
            {"offline", workflow_flags.offline},
        },
        as_json);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "vendor"}, as_json);
  }
  catch (const WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", kExitWorkspace, err.what(), "vendor"}, as_json);
  }
  catch (const ResolutionError &err)
  {
    return EmitError({"ResolutionError", kExitResolve, err.what(), "vendor"}, as_json);
  }
  catch (const FetchError &err)
  {
    return EmitError({"FetchError", kExitFetch, err.what(), "vendor"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "vendor"}, as_json);
  }
  catch (const VendorError &err)
  {
    return EmitError({"VendorError", kExitVendor, err.what(), "vendor"}, as_json);
  }
}

int HandlePack(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("pack");
  }

  PackRequest request;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "pack"}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--package")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--package requires a value", "pack"}, as_json);
      }
      request.package_name = args[index];
    }
    else if (args[index] == "--output")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--output requires a value", "pack"}, as_json);
      }
      request.output_path = args[index];
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for pack: " + args[index], "pack"}, as_json);
    }
  }

  try
  {
    const PackResult result = WriteSourcePackage(request);
    return EmitSuccess(
        {
            {"command", "pack"},
            {"message", "wrote source package: " + result.archive_path.string()},
            {"manifest_path", result.manifest_path.string()},
            {"package_root", result.package_root.string()},
            {"archive_path", result.archive_path.string()},
            {"archive_prefix", result.archive_prefix},
            {"package", result.package_name},
            {"version", result.package_version},
            {"files", result.file_count},
        },
        as_json);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "pack"}, as_json);
  }
  catch (const WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", kExitWorkspace, err.what(), "pack"}, as_json);
  }
  catch (const PackError &err)
  {
    return EmitError({"PackError", kExitPack, err.what(), "pack"}, as_json);
  }
}

int HandlePublish(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("publish");
  }

  PublishRequest request;
  std::optional<std::string> registry_root;
  bool dry_run = false;
  bool allow_plaintext_http = false;
  for (size_t index = 0; index < args.size(); ++index)
  {
    if (args[index] == "--manifest-path")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--manifest-path requires a value", "publish"}, as_json);
      }
      request.manifest_path = args[index];
    }
    else if (args[index] == "--package")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--package requires a value", "publish"}, as_json);
      }
      request.package_name = args[index];
    }
    else if (args[index] == "--output")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--output requires a value", "publish"}, as_json);
      }
      request.output_path = args[index];
    }
    else if (args[index] == "--registry")
    {
      if (++index >= args.size())
      {
        return EmitError({"UsageError", kExitUsage, "--registry requires a value", "publish"}, as_json);
      }
      registry_root = args[index];
    }
    else if (args[index] == "--dry-run")
    {
      dry_run = true;
    }
    else if (args[index] == "--allow-plaintext-http")
    {
      allow_plaintext_http = true;
    }
    else
    {
      return EmitError({"UsageError", kExitUsage, "unexpected argument for publish: " + args[index], "publish"}, as_json);
    }
  }

  if (!dry_run)
  {
    if (!registry_root.has_value())
    {
      return EmitError(
          {"UsageError", kExitUsage, "publish requires --registry <http(s)-url> unless --dry-run is set", "publish"},
          as_json);
    }
    if (!IsHttpRegistryRoot(*registry_root))
    {
      return EmitError(
          {
              "UsageError",
              kExitUsage,
              "publish --registry must use an http:// or https:// URL",
              "publish",
          },
          as_json);
    }

    try
    {
      const PublishResult candidate = PreparePublishCandidate(request);
      const RegistryWriteSecurityDecision security = ResolveRegistryWriteSecurity({
          .registry_root = *registry_root,
          .allow_plaintext_http = allow_plaintext_http,
      });
      return EmitSuccess(
          BuildHttpPublishPayload(
              candidate,
              security,
              RunRegistryV2ControlPlanePublish(security.registry_root, security.request_headers, candidate)),
          as_json);
    }
    catch (const ValidationError &err)
    {
      return EmitError({"ManifestError", kExitManifest, err.what(), "publish"}, as_json);
    }
    catch (const WorkspaceError &err)
    {
      return EmitError({"WorkspaceError", kExitWorkspace, err.what(), "publish"}, as_json);
    }
    catch (const PackError &err)
    {
      return EmitError({"PackError", kExitPack, err.what(), "publish"}, as_json);
    }
    catch (const PublishError &err)
    {
      return EmitError({"PublishError", kExitPublish, err.what(), "publish"}, as_json);
    }
  }

  try
  {
    const PublishResult result = PreparePublishCandidate(request);
    return EmitSuccess(
        {
            {"command", "publish"},
            {"mode", "dry-run"},
            {"message", "prepared publish candidate: " + result.archive_path.string()},
            {"manifest_path", result.manifest_path.string()},
            {"package_root", result.package_root.string()},
            {"archive_path", result.archive_path.string()},
            {"package", result.package_name},
            {"version", result.package_version},
            {"dependencies", result.dependency_count},
            {"dev_dependencies", result.dev_dependency_count},
        },
        as_json);
  }
  catch (const ValidationError &err)
  {
    return EmitError({"ManifestError", kExitManifest, err.what(), "publish"}, as_json);
  }
  catch (const WorkspaceError &err)
  {
    return EmitError({"WorkspaceError", kExitWorkspace, err.what(), "publish"}, as_json);
  }
  catch (const PackError &err)
  {
    return EmitError({"PackError", kExitPack, err.what(), "publish"}, as_json);
  }
  catch (const PublishError &err)
  {
    return EmitError({"PublishError", kExitPublish, err.what(), "publish"}, as_json);
  }
}

int HandleRegistry(const std::vector<std::string> &args, bool as_json)
{
  if (args.size() == 1 && args.front() == "--help")
  {
    return PrintCommandUsage("registry");
  }
  if (args.size() < 2 || args.front() != "trust")
  {
    return EmitError({"UsageError", kExitUsage, "registry requires the 'trust' subcommand", "registry"}, as_json);
  }

  const std::string action = args[1];
  try
  {
    const fs::path pafio_home = ResolvePafioHome();
    if (action == "import")
    {
      bool allow_dev = false;
      std::string source;
      for (size_t index = 2; index < args.size(); ++index)
      {
        if (args[index] == "--dev")
        {
          allow_dev = true;
          continue;
        }
        if (!source.empty())
        {
          return EmitError(
              {"UsageError",
               kExitUsage,
               "registry trust import accepts [--dev] <descriptor-url|descriptor-file>",
               "registry"},
              as_json);
        }
        source = args[index];
      }
      if (source.empty())
      {
        return EmitError(
            {"UsageError",
             kExitUsage,
             "registry trust import accepts [--dev] <descriptor-url|descriptor-file>",
             "registry"},
            as_json);
      }
      const RegistryTrustPin pin = ImportRegistryTrustDescriptor(
          pafio_home,
          source,
          RegistryTrustImportOptions{.allow_dev_unsigned = allow_dev});
      return EmitSuccess(
          {
              {"command", "registry trust import"},
              {"message", "imported registry trust descriptor for " + pin.registry_root},
              {"pafio_home", pafio_home.string()},
              {"dev_unsigned", allow_dev},
              {"pin", SerializeRegistryTrustPin(pin)},
          },
          as_json);
    }
    if (action == "status")
    {
      if (!(args.size() == 2 || (args.size() == 3 && args[2] == "--json")))
      {
        return EmitError({"UsageError", kExitUsage, "registry trust status accepts only --json", "registry"}, as_json);
      }
      json pins = json::array();
      for (const RegistryTrustPin &pin : LoadRegistryTrustPins(pafio_home))
      {
        pins.push_back(SerializeRegistryTrustPin(pin));
      }
      return EmitSuccess(
          {
              {"command", "registry trust status"},
              {"message", "loaded registry trust pins"},
              {"pafio_home", pafio_home.string()},
              {"pins", pins},
          },
          as_json);
    }
  }
  catch (const FetchError &err)
  {
    return EmitError({"FetchError", kExitFetch, err.what(), "registry"}, as_json);
  }
  catch (const CacheError &err)
  {
    return EmitError({"CacheError", kExitCache, err.what(), "registry"}, as_json);
  }

  return EmitError({"UsageError", kExitUsage, "registry trust supports only import and status", "registry"}, as_json);
}

}  // namespace pafio

#include "PafioCloud/Job.hpp"

#include "PafioCloud/Contract.hpp"

#include "PafioCore/Errors.hpp"

#include <cstdlib>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

std::optional<std::string> ValidateWorkflowInvocationImpl(
    std::string_view action,
    const pafio::BuildPlanRequest &request,
    const pafio::ProjectToolchainState &toolchain_state,
    const pafio::WorkflowInvocationOptions &options)
{
  const std::string normalized_action(action);
  if (normalized_action != "build" && normalized_action != "run" && normalized_action != "test")
  {
    return "workflow action must be one of: build, run, test";
  }

  const std::string effective_build_mode = request.build_mode.empty() ? toolchain_state.build_mode : request.build_mode;
  if (effective_build_mode.empty() || !pafio::IsSupportedBuildMode(effective_build_mode))
  {
    return "build currently supports only the 'minimal' mode";
  }

  const bool has_source_options =
      options.assume_yes || !options.allow_fetch || options.non_interactive || options.source_root.has_value() ||
      options.source_revision.has_value();
  if (toolchain_state.mode == pafio::kToolchainModeBinary && has_source_options)
  {
    return "source-build options require 'pafio use build'";
  }
  if (toolchain_state.mode == pafio::kToolchainModeBuild && options.styio_bin.has_value())
  {
    return "--styio-bin is only valid when the project toolchain mode is 'binary'";
  }

  if (normalized_action == "run")
  {
    if (request.select_lib)
    {
      return "run does not accept --lib";
    }
    if (request.test_name.has_value())
    {
      return "run does not accept --test";
    }
  }
  if (normalized_action == "test")
  {
    if (request.select_lib)
    {
      return "test does not accept --lib";
    }
    if (request.bin_name.has_value())
    {
      return "test does not accept --bin";
    }
  }
  if (normalized_action == "build" && request.test_name.has_value())
  {
    return "build does not accept --test";
  }

  return std::nullopt;
}

}  // namespace

namespace pafio
{

std::string ResolveSourceOriginBaseline()
{
  if (const char *explicit_origin = std::getenv("PAFIO_STYIO_SOURCE_ORIGIN"); explicit_origin != nullptr && explicit_origin[0] != '\0')
  {
    return explicit_origin;
  }
  return "https://github.com/SymPolicy/Styio.git";
}

CloudBuildJobRequest BuildCloudBuildJobRequest(
    std::string_view action,
    const BuildPlanRequest &request,
    const ProjectToolchainState &toolchain_state,
    const WorkflowInvocationOptions &options,
    const CloudExecutionPolicy &cloud_policy)
{
  if (const std::optional<std::string> error =
          ValidateWorkflowInvocationImpl(action, request, toolchain_state, options);
      error.has_value())
  {
    throw ValidationError(*error);
  }

  const std::string build_mode = request.build_mode.empty() ? toolchain_state.build_mode : request.build_mode;
  CloudBuildJobRequest job_request;
  job_request.action_ = std::string(action);
  job_request.manifest_path_ = request.manifest_path;
  job_request.toolchain_mode_ = toolchain_state.mode;
  job_request.channel_ = toolchain_state.channel;
  job_request.build_mode_ = build_mode.empty() ? std::string(pafio::kBuildModeMinimal) : build_mode;
  job_request.profile_ = request.profile;
  job_request.locked_ = options.locked;
  job_request.offline_ = options.offline;
  job_request.target_ = {
      .package_name = request.package_name,
      .bin_name = request.bin_name,
      .test_name = request.test_name,
      .select_lib = request.select_lib,
  };
  job_request.source_ = {
      .origin = ResolveSourceOriginBaseline(),
      .explicit_source_root = options.source_root,
      .requested_revision = options.source_revision,
      .resolved_revision = toolchain_state.source_revision,
      .allow_fetch = options.allow_fetch,
      .assume_yes = options.assume_yes,
      .non_interactive = options.non_interactive,
      .offline = options.offline,
  };
  job_request.cloud_policy_ = cloud_policy;
  return job_request;
}

json BuildCloudBuildJobRequestPayload(const CloudBuildJobRequest &request)
{
  json payload = {
      {"schema_version", request.schema_version()},
      {"api_path", request.api_path()},
      {"action", request.action()},
      {"manifest_path", fs::absolute(request.manifest_path()).string()},
      {"toolchain", {
                        {"mode", request.toolchain_mode()},
                        {"channel", request.channel()},
                        {"build_mode", request.build_mode()},
                    }},
      {"profile", request.profile()},
      {"workflow", {
                       {"locked", request.locked()},
                       {"offline", request.offline()},
                   }},
      {"target", {
                     {"package", request.target().package_name.has_value() ? json(*request.target().package_name) : json(nullptr)},
                     {"bin", request.target().bin_name.has_value() ? json(*request.target().bin_name) : json(nullptr)},
                     {"test", request.target().test_name.has_value() ? json(*request.target().test_name) : json(nullptr)},
                     {"lib", request.target().select_lib},
                 }},
      {"source", {
                     {"origin", request.source().origin},
                     {"allow_fetch", request.source().allow_fetch},
                     {"assume_yes", request.source().assume_yes},
                     {"non_interactive", request.source().non_interactive},
                     {"offline", request.source().offline},
                 }},
      {"cloud", SerializeCloudExecutionPolicy(request.cloud_policy())},
  };

  if (request.source().explicit_source_root.has_value())
  {
    payload["source"]["explicit_root"] = fs::absolute(*request.source().explicit_source_root).string();
  }
  if (request.source().requested_revision.has_value())
  {
    payload["source"]["requested_revision"] = *request.source().requested_revision;
  }
  if (request.source().resolved_revision.has_value())
  {
    payload["source"]["resolved_revision"] = *request.source().resolved_revision;
  }
  return payload;
}

}  // namespace pafio

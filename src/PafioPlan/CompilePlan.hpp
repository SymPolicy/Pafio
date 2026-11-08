#pragma once

#include "PafioResolve/Resolver.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pafio
{

struct ObservableStaticSnapshotRequest
{
  int schema_version = 1;
  std::vector<std::string> required_capabilities;
  // Transport input only: names the previous snapshot artifact so Styio can emit
  // a delta next to the new snapshot. Never part of the cache key.
  std::optional<std::filesystem::path> parent_snapshot_path;
};

struct RuntimeObservationSampling
{
  int numerator = 1;
  int denominator = 16;
  std::optional<long long> seed;
};

// Styio stage S3 runtime-events request. Pafio only forwards the fields the
// caller set; Styio applies its own defaults and validates every value.
struct RuntimeObservationRequest
{
  int version = 2;
  std::optional<std::string> mode;
  std::vector<std::string> required_capabilities;
  std::optional<int> lane_capacity;
  std::optional<int> priority_reserved;
  std::optional<int> producer_lanes;
  std::optional<RuntimeObservationSampling> sampling;
};

struct BuildPlanRequest
{
  std::filesystem::path manifest_path = "pafio.toml";
  std::string intent = "build";
  std::optional<std::string> package_name;
  std::optional<std::string> bin_name;
  std::optional<std::string> test_name;
  bool select_lib = false;
  std::string profile = "dev";
  std::optional<std::string> compiler_version;
  std::optional<std::string> compiler_channel;
  bool offline = false;
  std::optional<std::filesystem::path> vendor_root;
  std::optional<ObservableStaticSnapshotRequest> observable_static_snapshot;
  std::optional<RuntimeObservationRequest> runtime_observation;
};

struct BuildPlanResult
{
  std::filesystem::path manifest_path;
  std::filesystem::path workspace_root;
  std::filesystem::path build_root;
  std::filesystem::path artifact_dir;
  std::filesystem::path diag_dir;
  std::filesystem::path plan_path;
  std::string cache_key;
  std::string plan_json;
  std::string entry_package_id;
  std::string entry_package_name;
  std::string entry_target_kind;
  std::string entry_target_name;
  std::string profile_name;
  size_t package_count = 0;
};

BuildPlanResult WriteBuildCompilePlan(const BuildPlanRequest &request);
BuildPlanResult WriteBuildCompilePlan(
    const BuildPlanRequest &request,
    const ResolvedGraphResult &graph);

}  // namespace pafio

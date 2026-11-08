#pragma once

#include "PafioResolve/Resolver.hpp"

#include <filesystem>

#include <nlohmann/json.hpp>

namespace pafio
{

struct MetadataDocument
{
  nlohmann::json package;
  nlohmann::json workspace;
  nlohmann::json dependencies;
  nlohmann::json targets;
  nlohmann::json lock;
  nlohmann::json resolution;
  nlohmann::json vendor;
};

MetadataDocument BuildMetadataDocument(
    const std::filesystem::path &manifest_path,
    const ResolvedGraphResult &graph);
nlohmann::json SerializeMetadataV1(const MetadataDocument &document);

}  // namespace pafio

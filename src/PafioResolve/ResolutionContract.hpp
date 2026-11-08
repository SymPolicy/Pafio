#pragma once

#include "PafioResolve/Resolver.hpp"

#include <string>
#include <string_view>

namespace pafio
{

std::string SerializeResolutionCanonical(
    const ResolvedGraphResult &graph,
    std::string_view manifest_sha256,
    std::string_view lock_sha256);

}  // namespace pafio

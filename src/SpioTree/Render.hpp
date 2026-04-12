#pragma once

#include "SpioResolve/Resolver.hpp"

#include <string>

namespace spio
{

std::string RenderDependencyTreeText(const LockGenerationResult &graph);

}  // namespace spio

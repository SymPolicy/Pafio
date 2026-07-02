#pragma once

#include "PafioResolve/Resolver.hpp"

#include <string>

namespace pafio
{

std::string RenderDependencyTreeText(const LockGenerationResult &graph);

}  // namespace pafio

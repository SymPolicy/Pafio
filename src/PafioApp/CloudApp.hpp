#pragma once

#include <vector>
#include <string>

namespace pafio
{

int HandleCloudStatus(const std::vector<std::string> &args, bool as_json);
int HandleCloudPlan(const std::vector<std::string> &args, bool as_json);
int HandleCloud(const std::vector<std::string> &args, bool as_json);

}  // namespace pafio

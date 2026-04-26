#pragma once

#include <vector>
#include <string>

namespace spio
{

int HandleCloudStatus(const std::vector<std::string> &args, bool as_json);
int HandleCloudPlan(const std::vector<std::string> &args, bool as_json);
int HandleCloud(const std::vector<std::string> &args, bool as_json);

}  // namespace spio

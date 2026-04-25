#pragma once

#include <string_view>
#include <vector>

namespace spio
{

int HandleProjectGraph(const std::vector<std::string> &args, bool as_json);
int HandleCheck(const std::vector<std::string> &args, bool as_json);
int HandlePlanCommand(
    std::string_view command_name,
    std::string_view intent,
    bool allow_lib,
    bool allow_bin,
    bool allow_test,
    const std::vector<std::string> &args,
    bool as_json);
int HandleBuild(const std::vector<std::string> &args, bool as_json);
int HandleRun(const std::vector<std::string> &args, bool as_json);
int HandleTest(const std::vector<std::string> &args, bool as_json);

}  // namespace spio

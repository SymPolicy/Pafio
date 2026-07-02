#pragma once

#include <vector>
#include <string>

namespace pafio
{

int HandleUse(const std::vector<std::string> &args, bool as_json);
int HandleSet(const std::vector<std::string> &args, bool as_json);
int HandleInstall(const std::vector<std::string> &args, bool as_json);
int HandleToolStatus(const std::vector<std::string> &args, bool as_json);
int HandleToolInstall(const std::vector<std::string> &args, bool as_json);
int HandleToolUse(const std::vector<std::string> &args, bool as_json);
int HandleToolPin(const std::vector<std::string> &args, bool as_json);

}  // namespace pafio

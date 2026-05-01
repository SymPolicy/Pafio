#pragma once

#include <vector>
#include <string>

namespace spio
{

int HandleNew(const std::vector<std::string> &args, bool as_json);
int HandleInit(const std::vector<std::string> &args, bool as_json);
int HandleAdd(const std::vector<std::string> &args, bool as_json);
int HandleRemove(const std::vector<std::string> &args, bool as_json);
int HandleSync(const std::vector<std::string> &args, bool as_json);
int HandleFetch(const std::vector<std::string> &args, bool as_json);
int HandleLock(const std::vector<std::string> &args, bool as_json);
int HandleTree(const std::vector<std::string> &args, bool as_json);
int HandleVendor(const std::vector<std::string> &args, bool as_json);
int HandlePack(const std::vector<std::string> &args, bool as_json);
int HandlePublish(const std::vector<std::string> &args, bool as_json);
int HandleRegistry(const std::vector<std::string> &args, bool as_json);

}  // namespace spio

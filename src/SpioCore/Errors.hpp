#pragma once

#include <stdexcept>
#include <string>

namespace spio
{

inline constexpr int kExitSuccess = 0;
inline constexpr int kExitUsage = 2;
inline constexpr int kExitManifest = 10;
inline constexpr int kExitLock = 11;
inline constexpr int kExitWorkspace = 12;
inline constexpr int kExitResolve = 13;
inline constexpr int kExitFetch = 14;
inline constexpr int kExitCache = 15;
inline constexpr int kExitPack = 16;
inline constexpr int kExitPublish = 17;
inline constexpr int kExitToolInstall = 18;
inline constexpr int kExitVendor = 19;
inline constexpr int kExitPlan = 20;
inline constexpr int kExitContract = 21;
inline constexpr int kExitCompilerSpawn = 22;
inline constexpr int kExitCompiler = 23;
inline constexpr int kExitRun = 24;
inline constexpr int kExitTest = 25;
inline constexpr int kExitInternal = 30;
inline constexpr int kExitNotImplemented = 31;

class ValidationError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class WorkspaceError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class ResolutionError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class FetchError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class CacheError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class PackError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class PublishError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class ToolError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class VendorError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class PlanError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class CompatibilityError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class CompilerProbeError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

struct CommandError
{
  std::string category;
  int code = kExitInternal;
  std::string message;
  std::string command;
};

}  // namespace spio

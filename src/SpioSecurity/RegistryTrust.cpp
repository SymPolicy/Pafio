#include "SpioSecurity/RegistryTrust.hpp"

#include "SpioCore/Errors.hpp"
#include "SpioCore/Paths.hpp"
#include "SpioCore/Process.hpp"
#include "SpioCore/Sha256.hpp"
#include "SpioSecurity/RegistrySecurity.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

bool IsHttpSource(std::string_view value)
{
  return value.starts_with("http://") || value.starts_with("https://");
}

bool IsFileUrl(std::string_view value)
{
  return value.starts_with("file://");
}

std::string ReadFileText(const fs::path &path)
{
  std::ifstream in(path);
  if (!in)
  {
    throw spio::FetchError("failed to open registry trust descriptor: " + path.string());
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::string LoadDescriptorText(const std::string &source)
{
  if (IsHttpSource(source))
  {
    const spio::ProcessResult result = spio::RunProcess<spio::FetchError>({
        .program = "curl",
        .args = {"-fsSL", "--connect-timeout", "10", "--max-time", "30", source},
        .timeout = spio::kExternalProcessStepTimeout,
        .error_context = "registry trust descriptor fetch",
    });
    if (result.exit_code != 0)
    {
      throw spio::FetchError("failed to fetch registry trust descriptor: " + spio::DescribeProcessFailure(result));
    }
    return result.stdout_text;
  }
  if (IsFileUrl(source))
  {
    return ReadFileText(fs::path(source.substr(std::string("file://").size())));
  }
  return ReadFileText(fs::path(source));
}

std::string RequiredString(const json &payload, const char *field)
{
  if (!payload.contains(field) || !payload[field].is_string() || payload[field].get<std::string>().empty())
  {
    throw spio::FetchError(std::string("registry trust descriptor is missing field: ") + field);
  }
  return payload[field].get<std::string>();
}

json LoadTrustStore(const fs::path &spio_home)
{
  const fs::path store_path = spio::RegistryTrustStorePath(spio_home);
  if (!fs::exists(store_path))
  {
    return {{"schema_version", 1}, {"pins", json::array()}};
  }
  try
  {
    return json::parse(ReadFileText(store_path));
  }
  catch (const json::parse_error &)
  {
    throw spio::CacheError("registry trust store is not valid JSON: " + store_path.string());
  }
}

void WriteTrustStore(const fs::path &spio_home, const json &payload)
{
  const fs::path store_path = spio::RegistryTrustStorePath(spio_home);
  fs::create_directories(store_path.parent_path());
  const fs::path temp_path = store_path.parent_path() / (store_path.filename().string() + ".tmp");
  {
    std::ofstream out(temp_path);
    if (!out)
    {
      throw spio::CacheError("failed to open temporary registry trust store: " + temp_path.string());
    }
    out << payload.dump(2) << '\n';
    if (!out.good())
    {
      throw spio::CacheError("failed to write temporary registry trust store: " + temp_path.string());
    }
  }
  std::error_code ec;
  fs::rename(temp_path, store_path, ec);
  if (ec)
  {
    fs::remove(temp_path);
    throw spio::CacheError("failed to finalize registry trust store: " + store_path.string());
  }
}

spio::RegistryTrustPin PinFromJson(const json &payload)
{
  return {
      .registry_root = spio::NormalizeRegistryTrustRoot(RequiredString(payload, "registry_root")),
      .registry_name = payload.value("registry_name", ""),
      .root_sha256 = RequiredString(payload, "root_sha256"),
      .descriptor_sha256 = payload.value("descriptor_sha256", ""),
      .descriptor_source = payload.value("descriptor_source", ""),
      .control_plane_base_url = payload.value("control_plane_base_url", ""),
      .issued_at = payload.value("issued_at", ""),
      .expires = payload.value("expires", ""),
  };
}

}  // namespace

namespace spio
{

std::string NormalizeRegistryTrustRoot(std::string value)
{
  while (!value.empty() && value.back() == '/')
  {
    value.pop_back();
  }
  return value;
}

nlohmann::json SerializeRegistryTrustPin(const RegistryTrustPin &pin)
{
  return {
      {"registry_root", pin.registry_root},
      {"registry_name", pin.registry_name},
      {"root_sha256", pin.root_sha256},
      {"descriptor_sha256", pin.descriptor_sha256},
      {"descriptor_source", pin.descriptor_source},
      {"control_plane_base_url", pin.control_plane_base_url},
      {"issued_at", pin.issued_at},
      {"expires", pin.expires},
  };
}

std::vector<RegistryTrustPin> LoadRegistryTrustPins(const fs::path &spio_home)
{
  const json store = LoadTrustStore(spio_home);
  std::vector<RegistryTrustPin> pins;
  if (!store.contains("pins") || !store["pins"].is_array())
  {
    throw CacheError("registry trust store pins must be an array");
  }
  for (const json &entry : store["pins"])
  {
    if (!entry.is_object())
    {
      throw CacheError("registry trust store pin must be an object");
    }
    RegistryTrustPin pin = PinFromJson(entry);
    if (!IsRegistrySha256Digest(pin.root_sha256))
    {
      throw CacheError("registry trust store root_sha256 is not a lowercase sha256 digest");
    }
    pins.push_back(std::move(pin));
  }
  return pins;
}

std::optional<RegistryTrustPin> ResolveRegistryTrustPin(const fs::path &spio_home, const std::string &registry_root)
{
  const std::string normalized = NormalizeRegistryTrustRoot(registry_root);
  for (const RegistryTrustPin &pin : LoadRegistryTrustPins(spio_home))
  {
    if (pin.registry_root == normalized)
    {
      return pin;
    }
  }
  return std::nullopt;
}

RegistryTrustPin ImportRegistryTrustDescriptor(const fs::path &spio_home, const std::string &descriptor_source)
{
  const std::string descriptor_text = LoadDescriptorText(descriptor_source);
  json descriptor;
  try
  {
    descriptor = json::parse(descriptor_text);
  }
  catch (const json::parse_error &)
  {
    throw FetchError("registry trust descriptor is not valid JSON");
  }
  if (!descriptor.is_object())
  {
    throw FetchError("registry trust descriptor must be a JSON object");
  }
  if (descriptor.contains("returncode") && descriptor.value("returncode", -1) == 0 &&
      descriptor.contains("payload") && descriptor["payload"].is_object())
  {
    descriptor = descriptor["payload"];
  }

  RegistryTrustPin pin = PinFromJson(descriptor);
  if (!IsRegistrySha256Digest(pin.root_sha256))
  {
    throw FetchError("registry trust descriptor root_sha256 must be a lowercase sha256 digest");
  }
  pin.descriptor_sha256 = Sha256Text(descriptor_text);
  pin.descriptor_source = descriptor_source;

  json store = LoadTrustStore(spio_home);
  if (!store.contains("pins") || !store["pins"].is_array())
  {
    store["pins"] = json::array();
  }
  json &pins = store["pins"];
  bool replaced = false;
  for (json &entry : pins)
  {
    if (entry.is_object() && NormalizeRegistryTrustRoot(entry.value("registry_root", "")) == pin.registry_root)
    {
      entry = SerializeRegistryTrustPin(pin);
      replaced = true;
      break;
    }
  }
  if (!replaced)
  {
    pins.push_back(SerializeRegistryTrustPin(pin));
  }
  store["schema_version"] = 1;
  WriteTrustStore(spio_home, store);
  return pin;
}

}  // namespace spio

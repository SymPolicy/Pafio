#include "PafioSecurity/RegistryTrust.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include "PafioCore/AtomicFile.hpp"
#include "PafioCore/ContractLexicon.hpp"
#include "PafioCore/Errors.hpp"
#include "PafioCore/FileLock.hpp"
#include "PafioCore/Paths.hpp"
#include "PafioCore/Process.hpp"
#include "PafioCore/Sha256.hpp"
#include "PafioCore/ToolPaths.hpp"
#include "PafioSecurity/CanonicalJson.hpp"
#include "PafioSecurity/Ed25519.hpp"
#include "PafioSecurity/RegistrySecurity.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

bool
IsHttpSource(std::string_view value) {
  return value.starts_with("http://") || value.starts_with("https://");
}

bool
IsFileUrl(std::string_view value) {
  return value.starts_with("file://");
}

std::string
ReadFileText(const fs::path &path) {
  std::ifstream in(path);
  if (!in) {
    throw pafio::FetchError("failed to open registry trust descriptor: " + path.string());
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::string
LoadDescriptorText(const std::string &source) {
  if (IsHttpSource(source)) {
    if (pafio::UrlHasUserinfo(source)) {
      throw pafio::FetchError(
          "registry trust descriptor URL must not embed credentials in the URL userinfo; "
          "configure authentication separately");
    }
    const pafio::ProcessResult result = pafio::RunProcess<pafio::FetchError>({
      .program = pafio::ResolvedCurlPath(),
      .args = {"-fsSL", "--connect-timeout", "10", "--max-time", "30", source},
      .search_path = false,
      .timeout = pafio::kExternalProcessStepTimeout,
      .error_context = "registry trust descriptor fetch",
    });
    if (result.exit_code != 0) {
      throw pafio::FetchError("failed to fetch registry trust descriptor: " + pafio::DescribeProcessFailure(result));
    }
    return result.stdout_text;
  }
  if (IsFileUrl(source)) {
    return ReadFileText(fs::path(source.substr(std::string("file://").size())));
  }
  return ReadFileText(fs::path(source));
}

std::string
RequiredString(const json &payload, const char *field) {
  if (!payload.contains(field) || !payload[field].is_string() || payload[field].get<std::string>().empty()) {
    throw pafio::FetchError(std::string("registry trust descriptor is missing field: ") + field);
  }
  return payload[field].get<std::string>();
}

json
LoadTrustStore(const fs::path &pafio_home) {
  const fs::path store_path = pafio::RegistryTrustStorePath(pafio_home);
  if (!fs::exists(store_path)) {
    return {{"schema_version", 1}, {"pins", json::array()}};
  }
  try {
    return json::parse(ReadFileText(store_path));
  }
  catch (const json::parse_error &) {
    throw pafio::CacheError("registry trust store is not valid JSON: " + store_path.string());
  }
}

void
WriteTrustStore(const fs::path &pafio_home, const json &payload) {
  const fs::path store_path = pafio::RegistryTrustStorePath(pafio_home);
  pafio::AtomicWriteFile(store_path, payload.dump(2) + '\n');
}

pafio::RegistryTrustPin
PinFromJson(const json &payload) {
  pafio::RegistryTrustPin pin = {
    .registry_root = pafio::NormalizeRegistryTrustRoot(RequiredString(payload, "registry_root")),
    .registry_name = payload.value("registry_name", ""),
    .root_sha256 = RequiredString(payload, "root_sha256"),
    .descriptor_sha256 = payload.value("descriptor_sha256", ""),
    .descriptor_source = payload.value("descriptor_source", ""),
    .control_plane_base_url = payload.value("control_plane_base_url", ""),
    .issued_at = payload.value("issued_at", ""),
    .expires = payload.value("expires", ""),
    .timestamp_floor = payload.value("timestamp_floor", 0),
    .snapshot_floor = payload.value("snapshot_floor", 0),
  };
  // These URLs persist into the trust store and surface in status output;
  // embedded userinfo would leak credentials at every read.
  if (pafio::UrlHasUserinfo(pin.registry_root)) {
    throw pafio::FetchError("registry trust descriptor registry_root must not embed credentials in the URL userinfo");
  }
  if (pafio::UrlHasUserinfo(pin.control_plane_base_url)) {
    throw pafio::FetchError("registry trust descriptor control_plane_base_url must not embed credentials in the URL userinfo");
  }
  return pin;
}

std::optional<std::chrono::system_clock::time_point>
ParseUtcTimestamp(const std::string &value) {
  if (value.size() != 20U || value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' || value[16] != ':' || value[19] != 'Z') {
    return std::nullopt;
  }
  std::tm tm{};
  std::istringstream in(value.substr(0U, 19U));
  in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
  if (in.fail() || in.peek() != std::char_traits<char>::eof()) {
    return std::nullopt;
  }
  const int year = tm.tm_year + 1900;
  const int month = tm.tm_mon + 1;
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  constexpr int kDaysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (year < 1970 || month < 1 || month > 12 || tm.tm_mday < 1 || tm.tm_mday > (month == 2 && leap ? 29 : kDaysPerMonth[month - 1]) || tm.tm_hour < 0 || tm.tm_hour > 23 || tm.tm_min < 0 || tm.tm_min > 59 || tm.tm_sec < 0 || tm.tm_sec > 59) {
    return std::nullopt;
  }
#if defined(_WIN32)
  const time_t seconds = _mkgmtime(&tm);
#else
  const time_t seconds = timegm(&tm);
#endif
  if (seconds < 0) {
    return std::nullopt;
  }
  return std::chrono::system_clock::from_time_t(seconds);
}

void
EnforceDescriptorValidityWindow(const json &signed_payload) {
  const std::string issued_at = RequiredString(signed_payload, "issued_at");
  const std::string expires = RequiredString(signed_payload, "expires");
  const auto issued = ParseUtcTimestamp(issued_at);
  const auto expiry = ParseUtcTimestamp(expires);
  if (!issued.has_value() || !expiry.has_value()) {
    throw pafio::FetchError("registry trust descriptor issued_at/expires must be UTC timestamps");
  }
  const auto now = std::chrono::system_clock::now();
  constexpr auto kSkew = std::chrono::minutes{5};
  if (*issued > now + kSkew) {
    throw pafio::FetchError("registry trust descriptor issued_at is in the future");
  }
  if (*expiry < now - kSkew) {
    throw pafio::FetchError("registry trust descriptor has expired");
  }
  if (*expiry < *issued) {
    throw pafio::FetchError("registry trust descriptor expires before issued_at");
  }
}

std::unordered_map<std::string, std::string>
LoadDescriptorSignerKeys(const fs::path &pafio_home) {
  std::unordered_map<std::string, std::string> keys;
  if (const char *env_keys = std::getenv("PAFIO_DESCRIPTOR_SIGNER_KEYS"); env_keys != nullptr && *env_keys != '\0') {
    try {
      const json payload = json::parse(env_keys);
      if (!payload.is_object()) {
        throw pafio::FetchError("PAFIO_DESCRIPTOR_SIGNER_KEYS must be a JSON object of keyid->PEM");
      }
      for (auto it = payload.begin(); it != payload.end(); ++it) {
        if (!it.value().is_string()) {
          throw pafio::FetchError("PAFIO_DESCRIPTOR_SIGNER_KEYS values must be PEM strings");
        }
        keys[it.key()] = it.value().get<std::string>();
      }
    }
    catch (const json::parse_error &) {
      throw pafio::FetchError("PAFIO_DESCRIPTOR_SIGNER_KEYS is not valid JSON");
    }
  }

  const fs::path signers_path = pafio::RegistryTrustRoot(pafio_home) / "descriptor-signers.json";
  if (fs::exists(signers_path)) {
    try {
      const json payload = json::parse(ReadFileText(signers_path));
      if (!payload.is_object() || !payload.contains("keys") || !payload["keys"].is_object()) {
        throw pafio::FetchError("descriptor-signers.json must contain a keys object");
      }
      for (auto it = payload["keys"].begin(); it != payload["keys"].end(); ++it) {
        if (!it.value().is_object()) {
          continue;
        }
        if (it.value().contains("public") && it.value()["public"].is_string()) {
          keys[it.key()] = it.value()["public"].get<std::string>();
        }
        else if (it.value().contains("keyval") && it.value()["keyval"].is_object() && it.value()["keyval"].contains("public") && it.value()["keyval"]["public"].is_string()) {
          keys[it.key()] = it.value()["keyval"]["public"].get<std::string>();
        }
      }
    }
    catch (const json::parse_error &) {
      throw pafio::FetchError("descriptor-signers.json is not valid JSON");
    }
  }
  return keys;
}

void
VerifyDescriptorSignatures(const json &envelope, const std::unordered_map<std::string, std::string> &signer_keys) {
  if (!envelope.contains("signed") || !envelope["signed"].is_object()) {
    throw pafio::FetchError("registry trust descriptor must contain a signed object");
  }
  if (!envelope.contains("signatures") || !envelope["signatures"].is_array() || envelope["signatures"].empty()) {
    throw pafio::FetchError("registry trust descriptor is unsigned");
  }
  if (signer_keys.empty()) {
    throw pafio::FetchError(
      "no descriptor signer keys configured; install descriptor-signers.json under the trust store or set "
      "PAFIO_DESCRIPTOR_SIGNER_KEYS"
    );
  }
  const std::string message = pafio::CanonicalJsonBytes(envelope["signed"]);
  int valid = 0;
  for (const json &signature : envelope["signatures"]) {
    if (!signature.is_object()) {
      continue;
    }
    const std::string keyid = signature.value("keyid", "");
    const std::string sig = signature.value("sig", "");
    if (keyid.empty() || sig.empty()) {
      continue;
    }
    const auto it = signer_keys.find(keyid);
    if (it == signer_keys.end()) {
      continue;
    }
    if (pafio::Ed25519VerifyPem(it->second, message, sig)) {
      ++valid;
    }
  }
  if (valid < 1) {
    throw pafio::FetchError("registry trust descriptor signature is invalid or signer keyid is unknown");
  }
}

}  // namespace

namespace pafio
{

std::string
NormalizeRegistryTrustRoot(std::string value) {
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

nlohmann::json
SerializeRegistryTrustPin(const RegistryTrustPin &pin) {
  return {
    {"registry_root", pin.registry_root},
    {"registry_name", pin.registry_name},
    {"root_sha256", pin.root_sha256},
    {"descriptor_sha256", pin.descriptor_sha256},
    {"descriptor_source", pin.descriptor_source},
    {"control_plane_base_url", pin.control_plane_base_url},
    {"issued_at", pin.issued_at},
    {"expires", pin.expires},
    {"timestamp_floor", pin.timestamp_floor},
    {"snapshot_floor", pin.snapshot_floor},
  };
}

std::vector<RegistryTrustPin>
LoadRegistryTrustPins(const fs::path &pafio_home) {
  const json store = LoadTrustStore(pafio_home);
  std::vector<RegistryTrustPin> pins;
  if (!store.contains("pins") || !store["pins"].is_array()) {
    throw CacheError("registry trust store pins must be an array");
  }
  for (const json &entry : store["pins"]) {
    if (!entry.is_object()) {
      throw CacheError("registry trust store pin must be an object");
    }
    RegistryTrustPin pin = PinFromJson(entry);
    if (!IsRegistrySha256Digest(pin.root_sha256)) {
      throw CacheError("registry trust store root_sha256 is not a lowercase sha256 digest");
    }
    pins.push_back(std::move(pin));
  }
  return pins;
}

std::optional<RegistryTrustPin>
ResolveRegistryTrustPin(const fs::path &pafio_home, const std::string &registry_root) {
  const std::string normalized = NormalizeRegistryTrustRoot(registry_root);
  for (const RegistryTrustPin &pin : LoadRegistryTrustPins(pafio_home)) {
    if (pin.registry_root == normalized) {
      return pin;
    }
  }
  return std::nullopt;
}

RegistryTrustPin
ImportRegistryTrustDescriptor(
  const fs::path &pafio_home,
  const std::string &descriptor_source,
  const RegistryTrustImportOptions &options
) {
  const std::string descriptor_text = LoadDescriptorText(descriptor_source);
  json descriptor;
  try {
    descriptor = json::parse(descriptor_text);
  }
  catch (const json::parse_error &) {
    throw FetchError("registry trust descriptor is not valid JSON");
  }
  if (!descriptor.is_object()) {
    throw FetchError("registry trust descriptor must be a JSON object");
  }
  if (descriptor.contains("returncode") && descriptor.value("returncode", -1) == 0 && descriptor.contains("payload") && descriptor["payload"].is_object()) {
    descriptor = descriptor["payload"];
  }

  json signed_payload;
  const bool is_envelope = descriptor.contains("signed") && descriptor.contains("signatures");
  if (is_envelope) {
    const auto signer_keys = LoadDescriptorSignerKeys(pafio_home);
    VerifyDescriptorSignatures(descriptor, signer_keys);
    signed_payload = descriptor["signed"];
  }
  else if (options.allow_dev_unsigned) {
    std::cerr << "WARNING: importing UNSIGNED registry trust descriptor via --dev; "
                 "this is insecure and must not be used in production\n";
    signed_payload = descriptor;
  }
  else {
    throw FetchError(
      "registry trust descriptor must be a signed envelope {signed,signatures}; "
      "use --dev only for local unsigned descriptors"
    );
  }

  EnforceDescriptorValidityWindow(signed_payload);
  RegistryTrustPin pin = PinFromJson(signed_payload);
  if (!IsRegistrySha256Digest(pin.root_sha256)) {
    throw FetchError("registry trust descriptor root_sha256 must be a lowercase sha256 digest");
  }
  pin.descriptor_sha256 = Sha256Text(descriptor_text);
  pin.descriptor_source = descriptor_source;

  const FileLockGuard trust_lock =
    AcquireFileLock(RegistryTrustRoot(pafio_home), FileLockScope::kTrust);
  json store = LoadTrustStore(pafio_home);
  if (!store.contains("pins") || !store["pins"].is_array()) {
    store["pins"] = json::array();
  }
  json &pins = store["pins"];
  bool replaced = false;
  for (json &entry : pins) {
    if (entry.is_object() && NormalizeRegistryTrustRoot(entry.value("registry_root", "")) == pin.registry_root) {
      // Reimporting the same trust root keeps the learned replay floors;
      // a different root_sha256 is a new trust anchor and starts at zero.
      if (entry.value("root_sha256", "") == pin.root_sha256) {
        pin.timestamp_floor = entry.value("timestamp_floor", 0);
        pin.snapshot_floor = entry.value("snapshot_floor", 0);
      }
      entry = SerializeRegistryTrustPin(pin);
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    pins.push_back(SerializeRegistryTrustPin(pin));
  }
  store["schema_version"] = 1;
  WriteTrustStore(pafio_home, store);
  return pin;
}

void
RaiseRegistryTrustFloors(
  const fs::path &pafio_home,
  const std::string &registry_root,
  const int timestamp_version,
  const int snapshot_version
) {
  const FileLockGuard trust_lock =
    AcquireFileLock(RegistryTrustRoot(pafio_home), FileLockScope::kTrust);
  json store = LoadTrustStore(pafio_home);
  if (!store.contains("pins") || !store["pins"].is_array()) {
    return;
  }
  const std::string normalized = NormalizeRegistryTrustRoot(registry_root);
  bool changed = false;
  for (json &entry : store["pins"]) {
    if (!entry.is_object() || NormalizeRegistryTrustRoot(entry.value("registry_root", "")) != normalized) {
      continue;
    }
    const int timestamp_floor = entry.value("timestamp_floor", 0);
    const int snapshot_floor = entry.value("snapshot_floor", 0);
    const int raised_timestamp = std::max(timestamp_floor, timestamp_version);
    const int raised_snapshot = std::max(snapshot_floor, snapshot_version);
    if (raised_timestamp != timestamp_floor || raised_snapshot != snapshot_floor) {
      entry["timestamp_floor"] = raised_timestamp;
      entry["snapshot_floor"] = raised_snapshot;
      changed = true;
    }
    break;
  }
  if (changed) {
    WriteTrustStore(pafio_home, store);
  }
}

}  // namespace pafio

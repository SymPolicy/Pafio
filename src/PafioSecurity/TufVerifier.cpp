#include "PafioSecurity/TufVerifier.hpp"

#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include "PafioCore/Errors.hpp"
#include "PafioCore/Sha256.hpp"
#include "PafioSecurity/CanonicalJson.hpp"
#include "PafioSecurity/Ed25519.hpp"
#include "PafioSecurity/RegistrySecurity.hpp"

namespace
{

using json = nlohmann::json;
constexpr int kMaxTransparencyLogEntries = 1'000'000;

struct RolePolicy
{
  std::unordered_set<std::string> keyids;
  int threshold = 1;
};

[[noreturn]] void
Fail(pafio::TufVerifyResult &result, std::string code, std::string message) {
  result.ok = false;
  result.error_code = std::move(code);
  result.error = std::move(message);
  throw std::runtime_error(result.error);
}

const std::string &
RequireObjectBytes(
  const std::map<std::string, std::string> &objects,
  const std::string &relative_path,
  pafio::TufVerifyResult &result
) {
  const auto it = objects.find(relative_path);
  if (it == objects.end()) {
    Fail(result, "tuf.missing_object", "registry v2 object is missing: " + relative_path);
  }
  return it->second;
}

json
ParseObject(const std::string &text, const std::string &context, pafio::TufVerifyResult &result) {
  try {
    json payload = json::parse(text);
    if (!payload.is_object()) {
      Fail(result, "tuf.invalid_json", context + " must be a JSON object");
    }
    return payload;
  }
  catch (const json::parse_error &) {
    Fail(result, "tuf.invalid_json", context + " is not valid JSON");
  }
  return json::object();
}

json
RequireObjectField(const json &payload, const char *field, const std::string &context, pafio::TufVerifyResult &result) {
  if (!payload.contains(field) || !payload[field].is_object()) {
    Fail(result, "tuf.schema", context + " must contain object field '" + field + "'");
  }
  return payload[field];
}

std::string
RequireStringField(const json &payload, const char *field, const std::string &context, pafio::TufVerifyResult &result) {
  if (!payload.contains(field) || !payload[field].is_string() || payload[field].get<std::string>().empty()) {
    Fail(result, "tuf.schema", context + " must contain string field '" + field + "'");
  }
  return payload[field].get<std::string>();
}

int
RequireIntField(const json &payload, const char *field, const std::string &context, pafio::TufVerifyResult &result) {
  if (!payload.contains(field) || !payload[field].is_number_integer()) {
    Fail(result, "tuf.schema", context + " must contain integer field '" + field + "'");
  }
  return payload[field].get<int>();
}

bool
RequireBoolField(const json &payload, const char *field, const std::string &context, pafio::TufVerifyResult &result) {
  if (!payload.contains(field) || !payload[field].is_boolean()) {
    Fail(result, "tuf.schema", context + " must contain boolean field '" + field + "'");
  }
  return payload[field].get<bool>();
}

std::string
RequireSha256(const json &value, const std::string &context, pafio::TufVerifyResult &result) {
  if (!value.is_string() || !pafio::IsRegistrySha256Digest(value.get<std::string>())) {
    Fail(result, "tuf.schema", context + " must be a lowercase sha256 digest");
  }
  return value.get<std::string>();
}

std::string
NormalizeRelative(const std::string &relative_path, const std::string &context, pafio::TufVerifyResult &result) {
  try {
    return pafio::NormalizeRegistryObjectPath(relative_path, context).generic_string();
  }
  catch (const pafio::FetchError &error) {
    Fail(result, "tuf.path", error.what());
  }
  return {};
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
EnforceExpires(const json &signed_payload, const std::string &context, pafio::TufVerifyResult &result) {
  const std::string expires = RequireStringField(signed_payload, "expires", context, result);
  const auto parsed = ParseUtcTimestamp(expires);
  if (!parsed.has_value()) {
    Fail(result, "tuf.expires", context + " expires is not a valid UTC timestamp");
  }
  // 5-minute clock-skew tolerance.
  const auto now = std::chrono::system_clock::now() - std::chrono::minutes{5};
  if (*parsed < now) {
    Fail(result, "tuf.expired", context + " has expired");
  }
}

void
EnforceVersionMonotonic(
  const json &signed_payload,
  const std::string &context,
  const int previous_version,
  pafio::TufVerifyResult &result
) {
  const int version = RequireIntField(signed_payload, "version", context, result);
  if (version < 1) {
    Fail(result, "tuf.version", context + " version must be >= 1");
  }
  if (previous_version > 0 && version < previous_version) {
    Fail(result, "tuf.version_regression", context + " version regressed");
  }
}

RolePolicy
ParseRolePolicy(const json &roles, const char *role_name, pafio::TufVerifyResult &result) {
  const json role = RequireObjectField(roles, role_name, std::string("registry v2 role '") + role_name + "'", result);
  if (!role.contains("keyids") || !role["keyids"].is_array() || role["keyids"].empty()) {
    Fail(result, "tuf.schema", std::string("registry v2 role '") + role_name + "' keyids must be a non-empty array");
  }
  RolePolicy policy;
  for (const json &item : role["keyids"]) {
    if (!item.is_string() || item.get<std::string>().empty()) {
      Fail(result, "tuf.schema", std::string("registry v2 role '") + role_name + "' keyid must be a string");
    }
    policy.keyids.insert(item.get<std::string>());
  }
  if (role.contains("threshold")) {
    policy.threshold = RequireIntField(role, "threshold", std::string("registry v2 role '") + role_name + "'", result);
    if (policy.threshold < 1) {
      Fail(result, "tuf.schema", std::string("registry v2 role '") + role_name + "' threshold must be >= 1");
    }
  }
  return policy;
}

json
VerifyRoleEnvelope(
  const json &payload,
  const std::string &context,
  const std::string &required_type,
  const RolePolicy &policy,
  const std::map<std::string, std::string> &key_lookup,
  pafio::TufVerifyResult &result
) {
  const json signed_payload = RequireObjectField(payload, "signed", context, result);
  if (!payload.contains("signatures") || !payload["signatures"].is_array() || payload["signatures"].empty()) {
    Fail(result, "tuf.signature", context + " must contain at least one signature");
  }
  if (RequireStringField(signed_payload, "type", context, result) != required_type) {
    Fail(result, "tuf.schema", context + " type must equal '" + required_type + "'");
  }
  if (RequireStringField(signed_payload, "spec_version", context, result) != "1") {
    Fail(result, "tuf.schema", context + " spec_version must equal '1'");
  }

  const std::string message = pafio::CanonicalJsonBytes(signed_payload);
  int valid_signatures = 0;
  std::unordered_set<std::string> seen_keyids;
  for (const json &signature : payload["signatures"]) {
    if (!signature.is_object()) {
      Fail(result, "tuf.signature", context + " signature must be an object");
    }
    const std::string keyid = RequireStringField(signature, "keyid", context, result);
    const std::string sig = RequireStringField(signature, "sig", context, result);
    if (policy.keyids.find(keyid) == policy.keyids.end()) {
      continue;
    }
    if (!seen_keyids.insert(keyid).second) {
      continue;
    }
    const auto key_it = key_lookup.find(keyid);
    if (key_it == key_lookup.end()) {
      continue;
    }
    if (pafio::Ed25519VerifyPem(key_it->second, message, sig)) {
      ++valid_signatures;
    }
  }
  if (valid_signatures < policy.threshold) {
    Fail(result, "tuf.signature", context + " does not contain a valid trusted signature");
  }
  EnforceExpires(signed_payload, context, result);
  return signed_payload;
}

std::string
ObjectSha256(const std::string &bytes) {
  return pafio::Sha256Text(bytes);
}

void
VerifyMetaHash(
  const json &meta_object,
  const std::string &relative_path,
  const std::map<std::string, std::string> &objects,
  const std::string &context,
  pafio::TufVerifyResult &result,
  const bool check_length
) {
  const json hashes = RequireObjectField(meta_object, "hashes", context, result);
  const std::string expected = RequireSha256(hashes.contains("sha256") ? hashes["sha256"] : json(), context + " sha256", result);
  const std::string &bytes = RequireObjectBytes(objects, relative_path, result);
  if (ObjectSha256(bytes) != expected) {
    Fail(result, "tuf.hash_mismatch", context + " sha256 does not match file digest");
  }
  if (check_length && meta_object.contains("length")) {
    const int length = RequireIntField(meta_object, "length", context, result);
    if (static_cast<size_t>(length) != bytes.size()) {
      Fail(result, "tuf.length_mismatch", context + " length does not match file size");
    }
  }
}

struct TrustedRoot
{
  json signed_payload;
  std::map<std::string, std::string> key_lookup;
  RolePolicy root_role;
  RolePolicy timestamp_role;
  RolePolicy snapshot_role;
  RolePolicy targets_role;
  RolePolicy log_role;
  int version = 0;
};

TrustedRoot
VerifyRoot(
  const std::map<std::string, std::string> &objects,
  const std::optional<std::string> &trusted_root_sha256,
  pafio::TufVerifyResult &result
) {
  const std::string &root_bytes = RequireObjectBytes(objects, "trust/root.json", result);
  if (trusted_root_sha256.has_value() && ObjectSha256(root_bytes) != *trusted_root_sha256) {
    Fail(result, "tuf.root_pin", "registry v2 root metadata does not match the imported platform descriptor pin");
  }

  const json root_envelope = ParseObject(root_bytes, "registry v2 root metadata", result);
  const json root_signed = RequireObjectField(root_envelope, "signed", "registry v2 root metadata", result);
  if (!root_envelope.contains("signatures") || !root_envelope["signatures"].is_array() || root_envelope["signatures"].empty()) {
    Fail(result, "tuf.signature", "registry v2 root metadata must contain at least one signature");
  }
  if (RequireStringField(root_signed, "type", "registry v2 root metadata", result) != "root") {
    Fail(result, "tuf.schema", "registry v2 root metadata type must equal 'root'");
  }
  if (RequireStringField(root_signed, "spec_version", "registry v2 root metadata", result) != "1") {
    Fail(result, "tuf.schema", "registry v2 root metadata spec_version must equal '1'");
  }

  const json keys = RequireObjectField(root_signed, "keys", "registry v2 root metadata", result);
  const json roles = RequireObjectField(root_signed, "roles", "registry v2 root metadata", result);
  TrustedRoot trusted;
  trusted.signed_payload = root_signed;
  trusted.root_role = ParseRolePolicy(roles, "root", result);
  trusted.timestamp_role = ParseRolePolicy(roles, "timestamp", result);
  trusted.snapshot_role = ParseRolePolicy(roles, "snapshot", result);
  trusted.targets_role = ParseRolePolicy(roles, "targets", result);
  trusted.log_role = ParseRolePolicy(roles, "log", result);
  trusted.version = RequireIntField(root_signed, "version", "registry v2 root metadata", result);
  if (trusted.version < 1) {
    Fail(result, "tuf.version", "registry v2 root metadata version must be >= 1");
  }

  for (auto it = keys.begin(); it != keys.end(); ++it) {
    if (!it.value().is_object()) {
      Fail(result, "tuf.schema", "registry v2 key must be an object");
    }
    const json keyval = RequireObjectField(it.value(), "keyval", "registry v2 key", result);
    trusted.key_lookup[it.key()] = RequireStringField(keyval, "public", "registry v2 key", result);
  }

  const std::string message = pafio::CanonicalJsonBytes(root_signed);
  int valid_signatures = 0;
  std::unordered_set<std::string> seen_keyids;
  for (const json &signature : root_envelope["signatures"]) {
    if (!signature.is_object()) {
      Fail(result, "tuf.signature", "registry v2 root signature must be an object");
    }
    const std::string keyid = RequireStringField(signature, "keyid", "registry v2 root signature", result);
    if (trusted.root_role.keyids.find(keyid) == trusted.root_role.keyids.end()) {
      continue;
    }
    if (!seen_keyids.insert(keyid).second) {
      continue;
    }
    const auto key_it = trusted.key_lookup.find(keyid);
    if (key_it == trusted.key_lookup.end()) {
      continue;
    }
    if (pafio::Ed25519VerifyPem(key_it->second, message, RequireStringField(signature, "sig", "registry v2 root signature", result))) {
      ++valid_signatures;
    }
  }
  if (valid_signatures < trusted.root_role.threshold) {
    Fail(result, "tuf.signature", "registry v2 root metadata does not contain a valid root signature");
  }
  EnforceExpires(root_signed, "registry v2 root metadata", result);
  return trusted;
}

void
VerifyConfig(const std::map<std::string, std::string> &objects, pafio::TufVerifyResult &result) {
  const json config = ParseObject(RequireObjectBytes(objects, "config.json", result), "registry v2 config", result);
  if (config.value("protocol", "") != "pafio-static-registry" || config.value("protocol_version", 0) != 2) {
    Fail(result, "tuf.config", "registry v2 config does not declare the expected protocol/version");
  }
  const json capabilities = RequireObjectField(config, "capabilities", "registry v2 config", result);
  (void)RequireBoolField(capabilities, "append_only_index", "registry v2 config capabilities", result);
  (void)RequireBoolField(capabilities, "source_artifacts", "registry v2 config capabilities", result);
  (void)RequireBoolField(capabilities, "binary_artifacts", "registry v2 config capabilities", result);
  (void)RequireBoolField(capabilities, "transparency_log", "registry v2 config capabilities", result);
}

// A snapshot meta entry is a targets document when its signed payload
// declares type "targets" — not because of where it happens to live.
bool
IsTargetsObject(const std::map<std::string, std::string> &objects, const std::string &relative_path) {
  const auto it = objects.find(relative_path);
  if (it == objects.end()) {
    return false;
  }
  try {
    const json envelope = json::parse(it->second);
    return envelope.is_object() &&
      envelope.contains("signed") && envelope["signed"].is_object() &&
      envelope["signed"].value("type", "") == "targets";
  }
  catch (const json::parse_error &) {
    return false;
  }
}

pafio::TufVerifyResult
VerifyPackageChain(
  const std::map<std::string, std::string> &objects,
  const std::string &package_name,
  const std::string &package_namespace,
  const std::optional<std::string> &trusted_root_sha256,
  const std::string &targets_relative_path,
  const bool verify_full_registry,
  const int timestamp_floor,
  const int snapshot_floor
) {
  pafio::TufVerifyResult result;
  try {
    VerifyConfig(objects, result);
    TrustedRoot trusted = VerifyRoot(objects, trusted_root_sha256, result);
    result.root_version = trusted.version;

    const json timestamp_signed = VerifyRoleEnvelope(
      ParseObject(RequireObjectBytes(objects, "trust/timestamp.json", result), "registry v2 timestamp metadata", result),
      "registry v2 timestamp metadata",
      "timestamp",
      trusted.timestamp_role,
      trusted.key_lookup,
      result
    );
    EnforceVersionMonotonic(timestamp_signed, "registry v2 timestamp metadata", timestamp_floor, result);
    result.timestamp_version = RequireIntField(timestamp_signed, "version", "registry v2 timestamp metadata", result);

    const json timestamp_meta = RequireObjectField(timestamp_signed, "meta", "registry v2 timestamp meta", result);
    const json snapshot_meta = RequireObjectField(timestamp_meta, "trust/snapshot.json", "registry v2 timestamp meta", result);
    VerifyMetaHash(snapshot_meta, "trust/snapshot.json", objects, "registry v2 timestamp snapshot", result, true);

    const json snapshot_signed = VerifyRoleEnvelope(
      ParseObject(RequireObjectBytes(objects, "trust/snapshot.json", result), "registry v2 snapshot metadata", result),
      "registry v2 snapshot metadata",
      "snapshot",
      trusted.snapshot_role,
      trusted.key_lookup,
      result
    );
    EnforceVersionMonotonic(snapshot_signed, "registry v2 snapshot metadata", snapshot_floor, result);
    result.snapshot_version = RequireIntField(snapshot_signed, "version", "registry v2 snapshot metadata", result);

    const json snapshot_meta_entries = RequireObjectField(snapshot_signed, "meta", "registry v2 snapshot meta", result);
    const json log_meta_entries = RequireObjectField(snapshot_signed, "log_meta", "registry v2 snapshot log_meta", result);
    const json checkpoint_meta =
      RequireObjectField(log_meta_entries, "log/checkpoint.json", "registry v2 snapshot log_meta", result);
    VerifyMetaHash(checkpoint_meta, "log/checkpoint.json", objects, "registry v2 checkpoint", result, false);

    const json checkpoint_signed = VerifyRoleEnvelope(
      ParseObject(RequireObjectBytes(objects, "log/checkpoint.json", result), "registry v2 transparency checkpoint", result),
      "registry v2 transparency checkpoint",
      "checkpoint",
      trusted.log_role,
      trusted.key_lookup,
      result
    );

    const std::string targets_relative =
      NormalizeRelative(targets_relative_path, "registry v2 targets path", result);
    {
      bool found = false;
      for (auto it = snapshot_meta_entries.begin(); it != snapshot_meta_entries.end(); ++it) {
        const std::string normalized = NormalizeRelative(it.key(), "registry v2 snapshot meta path", result);
        if (normalized == targets_relative) {
          VerifyMetaHash(it.value(), normalized, objects, "registry v2 snapshot targets", result, false);
          found = true;
          break;
        }
      }
      if (!found) {
        Fail(result, "tuf.targets_missing", "registry v2 snapshot does not list targets for namespace " + package_namespace);
      }
    }

    const json targets_signed = VerifyRoleEnvelope(
      ParseObject(RequireObjectBytes(objects, targets_relative, result), "registry v2 targets metadata", result),
      "registry v2 targets metadata",
      "targets",
      trusted.targets_role,
      trusted.key_lookup,
      result
    );
    if (RequireStringField(targets_signed, "namespace", "registry v2 targets metadata", result) != package_namespace) {
      Fail(result, "tuf.namespace", "registry v2 targets namespace mismatch");
    }
    const json packages = RequireObjectField(targets_signed, "packages", "registry v2 targets packages", result);
    if (!packages.contains(package_name) || !packages[package_name].is_object()) {
      Fail(result, "tuf.package_missing", "registry v2 targets metadata is missing package: " + package_name);
    }

    if (verify_full_registry) {
      for (auto it = snapshot_meta_entries.begin(); it != snapshot_meta_entries.end(); ++it) {
        const std::string normalized = NormalizeRelative(it.key(), "registry v2 snapshot meta path", result);
        VerifyMetaHash(it.value(), normalized, objects, "registry v2 snapshot meta " + normalized, result, false);
        if (IsTargetsObject(objects, normalized)) {
          (void)VerifyRoleEnvelope(
            ParseObject(RequireObjectBytes(objects, normalized, result), "registry v2 targets metadata", result),
            "registry v2 targets metadata '" + normalized + "'",
            "targets",
            trusted.targets_role,
            trusted.key_lookup,
            result
          );
        }
      }

      const int tree_size = RequireIntField(checkpoint_signed, "tree_size", "registry v2 checkpoint", result);
      if (tree_size < 0 || tree_size > kMaxTransparencyLogEntries) {
        Fail(
          result,
          "tuf.log_size",
          "registry v2 checkpoint tree_size must be between 0 and " + std::to_string(kMaxTransparencyLogEntries)
        );
      }
      const std::string root_hash = RequireSha256(
        checkpoint_signed.contains("root_hash") ? checkpoint_signed["root_hash"] : json(),
        "registry v2 checkpoint root_hash",
        result
      );
      std::string computed(32, '\0');
      for (int sequence = 1; sequence <= tree_size; ++sequence) {
        std::ostringstream leaf_name;
        leaf_name << "log/leaves/" << std::setw(12) << std::setfill('0') << sequence << ".json";
        const std::string leaf_relative = leaf_name.str();
        const json leaf = ParseObject(RequireObjectBytes(objects, leaf_relative, result), "registry v2 log leaf", result);
        if (RequireIntField(leaf, "sequence", "registry v2 log leaf", result) != sequence) {
          Fail(result, "tuf.log_sequence", "registry v2 log leaf sequence mismatch");
        }
        const std::string leaf_hash_hex = ObjectSha256(pafio::CanonicalJsonBytes(leaf));
        std::string leaf_hash_raw;
        leaf_hash_raw.reserve(32);
        for (size_t i = 0; i + 1 < leaf_hash_hex.size(); i += 2) {
          const auto nibble = [](const char ch) -> int
          {
            if (ch >= '0' && ch <= '9') {
              return ch - '0';
            }
            return 10 + (ch - 'a');
          };
          leaf_hash_raw.push_back(static_cast<char>((nibble(leaf_hash_hex[i]) << 4) | nibble(leaf_hash_hex[i + 1])));
        }
        computed = pafio::Sha256Raw(computed + leaf_hash_raw);
      }
      std::ostringstream computed_hex;
      computed_hex << std::hex << std::setfill('0');
      for (const unsigned char ch : computed) {
        computed_hex << std::setw(2) << static_cast<int>(ch);
      }
      if (computed_hex.str() != root_hash) {
        Fail(result, "tuf.log_root", "registry v2 transparency checkpoint root hash mismatch");
      }
    }

    result.ok = true;
    result.targets = pafio::TufVerifiedTargets{
      .signed_payload = targets_signed,
      .relative_path = targets_relative,
      .sha256 = ObjectSha256(RequireObjectBytes(objects, targets_relative, result)),
    };
    return result;
  }
  catch (const std::runtime_error &) {
    result.ok = false;
    if (result.error.empty()) {
      result.error = "registry v2 TUF verification failed";
      result.error_code = "tuf.failed";
    }
    return result;
  }
}

}  // namespace

namespace pafio
{

TufVerifyResult
VerifyTufChainForPackage(
  const std::map<std::string, std::string> &objects_by_relative_path,
  const std::string &package_name,
  const std::string &package_namespace,
  const std::optional<std::string> &trusted_root_sha256,
  const std::string &targets_relative_path,
  const int timestamp_floor,
  const int snapshot_floor
) {
  return VerifyPackageChain(
    objects_by_relative_path,
    package_name,
    package_namespace,
    trusted_root_sha256,
    targets_relative_path,
    false,
    timestamp_floor,
    snapshot_floor
  );
}

TufVerifyResult
VerifyTufRegistryRoot(const std::map<std::string, std::string> &objects_by_relative_path) {
  // Full-registry mode still needs a package namespace to locate targets; scan snapshot meta.
  TufVerifyResult result;
  try {
    VerifyConfig(objects_by_relative_path, result);
    TrustedRoot trusted = VerifyRoot(objects_by_relative_path, std::nullopt, result);
    result.root_version = trusted.version;

    const json timestamp_signed = VerifyRoleEnvelope(
      ParseObject(
        RequireObjectBytes(objects_by_relative_path, "trust/timestamp.json", result),
        "registry v2 timestamp metadata",
        result
      ),
      "registry v2 timestamp metadata",
      "timestamp",
      trusted.timestamp_role,
      trusted.key_lookup,
      result
    );
    result.timestamp_version = RequireIntField(timestamp_signed, "version", "registry v2 timestamp metadata", result);
    const json timestamp_meta = RequireObjectField(timestamp_signed, "meta", "registry v2 timestamp meta", result);
    VerifyMetaHash(
      RequireObjectField(timestamp_meta, "trust/snapshot.json", "registry v2 timestamp meta", result),
      "trust/snapshot.json",
      objects_by_relative_path,
      "registry v2 timestamp snapshot",
      result,
      true
    );

    const json snapshot_signed = VerifyRoleEnvelope(
      ParseObject(
        RequireObjectBytes(objects_by_relative_path, "trust/snapshot.json", result),
        "registry v2 snapshot metadata",
        result
      ),
      "registry v2 snapshot metadata",
      "snapshot",
      trusted.snapshot_role,
      trusted.key_lookup,
      result
    );
    result.snapshot_version = RequireIntField(snapshot_signed, "version", "registry v2 snapshot metadata", result);
    const json snapshot_meta_entries = RequireObjectField(snapshot_signed, "meta", "registry v2 snapshot meta", result);
    const json log_meta_entries = RequireObjectField(snapshot_signed, "log_meta", "registry v2 snapshot log_meta", result);
    VerifyMetaHash(
      RequireObjectField(log_meta_entries, "log/checkpoint.json", "registry v2 snapshot log_meta", result),
      "log/checkpoint.json",
      objects_by_relative_path,
      "registry v2 checkpoint",
      result,
      false
    );

    (void)VerifyRoleEnvelope(
      ParseObject(
        RequireObjectBytes(objects_by_relative_path, "log/checkpoint.json", result),
        "registry v2 transparency checkpoint",
        result
      ),
      "registry v2 transparency checkpoint",
      "checkpoint",
      trusted.log_role,
      trusted.key_lookup,
      result
    );

    std::string first_package;
    std::string first_namespace;
    std::string first_targets_relative;
    for (auto it = snapshot_meta_entries.begin(); it != snapshot_meta_entries.end(); ++it) {
      const std::string normalized = NormalizeRelative(it.key(), "registry v2 snapshot meta path", result);
      VerifyMetaHash(it.value(), normalized, objects_by_relative_path, "registry v2 snapshot meta", result, false);
      if (IsTargetsObject(objects_by_relative_path, normalized)) {
        const json targets_signed = VerifyRoleEnvelope(
          ParseObject(
            RequireObjectBytes(objects_by_relative_path, normalized, result),
            "registry v2 targets metadata",
            result
          ),
          "registry v2 targets metadata '" + normalized + "'",
          "targets",
          trusted.targets_role,
          trusted.key_lookup,
          result
        );
        const json packages = RequireObjectField(targets_signed, "packages", "registry v2 targets packages", result);
        if (!packages.empty() && first_package.empty()) {
          first_package = packages.begin().key();
          first_namespace = RequireStringField(targets_signed, "namespace", "registry v2 targets metadata", result);
          first_targets_relative = normalized;
        }
      }
    }
    if (first_package.empty()) {
      result.ok = true;
      return result;
    }
    return VerifyPackageChain(
      objects_by_relative_path,
      first_package,
      first_namespace,
      std::nullopt,
      first_targets_relative,
      true,
      0,
      0
    );
  }
  catch (const std::runtime_error &) {
    result.ok = false;
    if (result.error.empty()) {
      result.error = "registry v2 TUF verification failed";
      result.error_code = "tuf.failed";
    }
    return result;
  }
}

}  // namespace pafio

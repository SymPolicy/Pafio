#pragma once

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace pafio
{

// Canonical registry JSON uses recursively sorted object keys, compact
// separators, unescaped UTF-8, and no insignificant whitespace.
inline void AppendCanonicalJson(std::ostringstream &out, const nlohmann::json &value)
{
  if (value.is_null())
  {
    out << "null";
    return;
  }
  if (value.is_boolean())
  {
    out << (value.get<bool>() ? "true" : "false");
    return;
  }
  if (value.is_number_integer() || value.is_number_unsigned())
  {
    out << value.dump();
    return;
  }
  if (value.is_number_float())
  {
    out << value.dump();
    return;
  }
  if (value.is_string())
  {
    // ensure_ascii=false: dump string with UTF-8 preserved.
    out << nlohmann::json(value.get<std::string>()).dump(-1, ' ', false);
    return;
  }
  if (value.is_array())
  {
    out << '[';
    bool first = true;
    for (const auto &item : value)
    {
      if (!first)
      {
        out << ',';
      }
      first = false;
      AppendCanonicalJson(out, item);
    }
    out << ']';
    return;
  }
  if (value.is_object())
  {
    std::vector<std::string> keys;
    keys.reserve(value.size());
    for (auto it = value.begin(); it != value.end(); ++it)
    {
      keys.push_back(it.key());
    }
    std::sort(keys.begin(), keys.end());
    out << '{';
    bool first = true;
    for (const std::string &key : keys)
    {
      if (!first)
      {
        out << ',';
      }
      first = false;
      out << nlohmann::json(key).dump(-1, ' ', false);
      out << ':';
      AppendCanonicalJson(out, value.at(key));
    }
    out << '}';
    return;
  }
  out << value.dump(-1, ' ', false);
}

inline std::string CanonicalJsonBytes(const nlohmann::json &value)
{
  std::ostringstream out;
  AppendCanonicalJson(out, value);
  return out.str();
}

}  // namespace pafio

#pragma once

#include <string>
#include <string_view>

namespace pafio
{

inline std::string
QuoteTomlString(std::string_view value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string quoted;
  quoted.reserve(value.size() + 2U);
  quoted.push_back('"');
  for (const unsigned char ch : value) {
    switch (ch) {
      case '"':
        quoted += "\\\"";
        break;
      case '\\':
        quoted += "\\\\";
        break;
      case '\b':
        quoted += "\\b";
        break;
      case '\t':
        quoted += "\\t";
        break;
      case '\n':
        quoted += "\\n";
        break;
      case '\f':
        quoted += "\\f";
        break;
      case '\r':
        quoted += "\\r";
        break;
      default:
        if (ch < 0x20U || ch == 0x7fU) {
          quoted += "\\u00";
          quoted.push_back(kHex[ch >> 4U]);
          quoted.push_back(kHex[ch & 0x0fU]);
        }
        else {
          quoted.push_back(static_cast<char>(ch));
        }
        break;
    }
  }
  quoted.push_back('"');
  return quoted;
}

}  // namespace pafio

#pragma once

#include <cctype>
#include <string>
#include <unordered_set>

namespace namevalidation {
inline bool isCppKeyword(const std::string &_s) {
  static const std::unordered_set<std::string> kKeywords = {
      "alignas",       "alignof",     "and",
      "and_eq",        "asm",         "auto",
      "bitand",        "bitor",       "bool",
      "break",         "case",        "catch",
      "char",          "char8_t",     "char16_t",
      "char32_t",      "class",       "compl",
      "concept",       "const",       "consteval",
      "constexpr",     "constinit",   "const_cast",
      "continue",      "co_await",    "co_return",
      "co_yield",      "decltype",    "default",
      "delete",        "do",          "double",
      "dynamic_cast",  "else",        "enum",
      "explicit",      "export",      "extern",
      "false",         "float",       "for",
      "friend",        "goto",        "if",
      "inline",        "int",         "long",
      "mutable",       "namespace",   "new",
      "noexcept",      "not",         "not_eq",
      "nullptr",       "operator",    "or",
      "or_eq",         "private",     "protected",
      "public",        "register",    "reinterpret_cast",
      "requires",      "return",      "short",
      "signed",        "sizeof",      "static",
      "static_assert", "static_cast", "struct",
      "switch",        "template",    "this",
      "thread_local",  "throw",       "true",
      "try",           "typedef",     "typeid",
      "typename",      "union",       "unsigned",
      "using",         "virtual",     "void",
      "volatile",      "wchar_t",     "while",
      "xor",           "xor_eq"};
  return kKeywords.count(_s) != 0;
}

inline bool isValidIdentifier(const std::string &_s) {
  if (_s.empty())
    return false;
  const unsigned char first = static_cast<unsigned char>(_s[0]);
  if (!(std::isalpha(first) || first == '_'))
    return false;
  for (char c : _s) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (!(std::isalnum(uc) || uc == '_'))
      return false;
  }
  return !isCppKeyword(_s);
}
} // namespace namevalidation

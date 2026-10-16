#pragma once
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace clipper {

struct I18nEntry {
  const char* key;
  const char* it;
  const char* en;
};

const std::vector<I18nEntry>& i18nTable();
void setLanguage(std::string_view lang);  // "it" (default) or "en"
const char* tr(std::string_view key);     // unknown keys return the key itself

template <class... A>
std::string trf(std::string_view key, A&&... args) {
  return std::vformat(tr(key), std::make_format_args(args...));
}

}  // namespace clipper

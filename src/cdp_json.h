#ifndef WINRPA_CDP_JSON_H
#define WINRPA_CDP_JSON_H

#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace winrpa {
namespace cdp {

struct JsonValue {
  enum class Type {
    kNull,
    kBool,
    kNumber,
    kString,
    kArray,
    kObject
  };

  Type type = Type::kNull;
  bool bool_val = false;
  bool is_int = false;
  std::int64_t int_val = 0;
  double num_val = 0.0;
  std::string str_val;
  std::vector<JsonValue> arr_val;
  std::vector<std::pair<std::string, JsonValue>> obj_val;

  static JsonValue make_null() { return JsonValue{}; }

  static JsonValue make_bool(bool b) {
    JsonValue v;
    v.type = Type::kBool;
    v.bool_val = b;
    return v;
  }

  static JsonValue make_int(std::int64_t i) {
    JsonValue v;
    v.type = Type::kNumber;
    v.is_int = true;
    v.int_val = i;
    v.num_val = static_cast<double>(i);
    return v;
  }

  static JsonValue make_double(double d) {
    JsonValue v;
    v.type = Type::kNumber;
    v.is_int = false;
    v.num_val = d;
    return v;
  }

  static JsonValue make_string(std::string s) {
    JsonValue v;
    v.type = Type::kString;
    v.str_val = std::move(s);
    return v;
  }

  static JsonValue make_array(std::vector<JsonValue> items = {}) {
    JsonValue v;
    v.type = Type::kArray;
    v.arr_val = std::move(items);
    return v;
  }

  static JsonValue make_object(std::vector<std::pair<std::string, JsonValue>> fields = {}) {
    JsonValue v;
    v.type = Type::kObject;
    v.obj_val = std::move(fields);
    return v;
  }

  bool is_null() const noexcept { return type == Type::kNull; }
  bool is_bool() const noexcept { return type == Type::kBool; }
  bool is_number() const noexcept { return type == Type::kNumber; }
  bool is_string() const noexcept { return type == Type::kString; }
  bool is_array() const noexcept { return type == Type::kArray; }
  bool is_object() const noexcept { return type == Type::kObject; }

  const JsonValue* find(const std::string& key) const noexcept {
    if (type != Type::kObject) {
      return nullptr;
    }
    for (const auto& kv : obj_val) {
      if (kv.first == key) {
        return &kv.second;
      }
    }
    return nullptr;
  }

  std::string get_string(const std::string& key, const std::string& fallback = "") const {
    const JsonValue* p = find(key);
    if (p != nullptr && p->is_string()) {
      return p->str_val;
    }
    return fallback;
  }

  std::int64_t get_int64(const std::string& key, std::int64_t fallback = 0) const {
    const JsonValue* p = find(key);
    if (p != nullptr && p->is_number()) {
      return p->is_int ? p->int_val : static_cast<std::int64_t>(p->num_val);
    }
    return fallback;
  }

  double get_double(const std::string& key, double fallback = 0.0) const {
    const JsonValue* p = find(key);
    if (p != nullptr && p->is_number()) {
      return p->num_val;
    }
    return fallback;
  }

  bool get_bool(const std::string& key, bool fallback = false) const {
    const JsonValue* p = find(key);
    if (p != nullptr && p->is_bool()) {
      return p->bool_val;
    }
    return fallback;
  }
};

inline void append_utf8_codepoint(std::string& out, std::uint32_t cp) {
  if (cp <= 0x7FU) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FFU) {
    out.push_back(static_cast<char>(0xC0U | ((cp >> 6U) & 0x1FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else if (cp <= 0xFFFFU) {
    out.push_back(static_cast<char>(0xE0U | ((cp >> 12U) & 0x0FU)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else if (cp <= 0x10FFFFU) {
    out.push_back(static_cast<char>(0xF0U | ((cp >> 18U) & 0x07U)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
  } else {
    throw std::runtime_error("Unicode code point out of range in JSON string.");
  }
}

class JsonParser {
 public:
  explicit JsonParser(const std::string& src) : src_(src), pos_(0U) {}

  JsonValue parse() {
    skip_ws();
    JsonValue val = parse_value(0);
    skip_ws();
    if (pos_ != src_.size()) {
      throw std::runtime_error("Unexpected trailing characters in JSON input.");
    }
    return val;
  }

 private:
  static constexpr int kMaxDepth = 64;
  const std::string& src_;
  std::size_t pos_;

  void skip_ws() noexcept {
    while (pos_ < src_.size()) {
      const char ch = src_[pos_];
      if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  char peek() const {
    if (pos_ >= src_.size()) {
      throw std::runtime_error("Unexpected end of JSON input.");
    }
    return src_[pos_];
  }

  char take() {
    const char ch = peek();
    ++pos_;
    return ch;
  }

  void expect_literal(const char* lit) {
    for (std::size_t i = 0U; lit[i] != '\0'; ++i) {
      if (pos_ >= src_.size() || src_[pos_] != lit[i]) {
        throw std::runtime_error("Invalid JSON literal token.");
      }
      ++pos_;
    }
  }

  std::uint32_t parse_hex4() {
    if (pos_ + 4U > src_.size()) {
      throw std::runtime_error("Truncated \\uXXXX escape in JSON string.");
    }
    std::uint32_t val = 0U;
    for (int i = 0; i < 4; ++i) {
      const char ch = src_[pos_++];
      val <<= 4U;
      if (ch >= '0' && ch <= '9') {
        val |= static_cast<std::uint32_t>(ch - '0');
      } else if (ch >= 'a' && ch <= 'f') {
        val |= static_cast<std::uint32_t>(10 + (ch - 'a'));
      } else if (ch >= 'A' && ch <= 'F') {
        val |= static_cast<std::uint32_t>(10 + (ch - 'A'));
      } else {
        throw std::runtime_error("Invalid hex digit in \\uXXXX JSON escape.");
      }
    }
    return val;
  }

  std::string parse_string() {
    if (take() != '"') {
      throw std::runtime_error("Expected '\"' at start of JSON string.");
    }
    std::string out;
    while (pos_ < src_.size()) {
      const char ch = src_[pos_++];
      if (ch == '"') {
        return out;
      }
      if (static_cast<unsigned char>(ch) < 0x20U) {
        throw std::runtime_error("Unescaped control byte inside JSON string.");
      }
      if (ch != '\\') {
        out.push_back(ch);
        continue;
      }
      const char esc = take();
      switch (esc) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          const std::uint32_t hi = parse_hex4();
          if (hi >= 0xD800U && hi <= 0xDBFFU) {
            if (pos_ + 2U <= src_.size() && src_[pos_] == '\\' && src_[pos_ + 1U] == 'u') {
              pos_ += 2U;
              const std::uint32_t lo = parse_hex4();
              if (lo >= 0xDC00U && lo <= 0xDFFFU) {
                const std::uint32_t cp =
                    0x10000U + (((hi - 0xD800U) << 10U) | (lo - 0xDC00U));
                append_utf8_codepoint(out, cp);
                break;
              }
            }
            throw std::runtime_error("Invalid UTF-16 surrogate pair in JSON string.");
          }
          append_utf8_codepoint(out, hi);
          break;
        }
        default:
          throw std::runtime_error("Unsupported backslash escape in JSON string.");
      }
    }
    throw std::runtime_error("Unterminated JSON string.");
  }

  JsonValue parse_number() {
    const std::size_t start = pos_;
    if (src_[pos_] == '-') {
      ++pos_;
    }
    if (pos_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
      throw std::runtime_error("Invalid JSON number.");
    }
    if (src_[pos_] == '0') {
      ++pos_;
    } else {
      while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
        ++pos_;
      }
    }
    bool is_float = false;
    if (pos_ < src_.size() && src_[pos_] == '.') {
      is_float = true;
      ++pos_;
      if (pos_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
        throw std::runtime_error("Invalid fractional part in JSON number.");
      }
      while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
        ++pos_;
      }
    }
    if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
      is_float = true;
      ++pos_;
      if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) {
        ++pos_;
      }
      if (pos_ >= src_.size() || !std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
        throw std::runtime_error("Invalid exponent in JSON number.");
      }
      while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
        ++pos_;
      }
    }
    const std::string token = src_.substr(start, pos_ - start);
    if (!is_float) {
      char* end_ptr = nullptr;
      const long long ival = std::strtoll(token.c_str(), &end_ptr, 10);
      if (end_ptr != nullptr && *end_ptr == '\0') {
        return JsonValue::make_int(static_cast<std::int64_t>(ival));
      }
    }
    char* d_end = nullptr;
    const double dval = std::strtod(token.c_str(), &d_end);
    if (d_end == token.c_str()) {
      throw std::runtime_error("Failed to parse JSON floating-point number.");
    }
    return JsonValue::make_double(dval);
  }

  JsonValue parse_array(int depth) {
    if (depth >= kMaxDepth) {
      throw std::runtime_error("JSON nesting depth limit exceeded.");
    }
    take();
    skip_ws();
    std::vector<JsonValue> items;
    if (pos_ < src_.size() && src_[pos_] == ']') {
      ++pos_;
      return JsonValue::make_array(std::move(items));
    }
    for (;;) {
      skip_ws();
      items.push_back(parse_value(depth + 1));
      skip_ws();
      const char ch = take();
      if (ch == ']') {
        break;
      }
      if (ch != ',') {
        throw std::runtime_error("Expected ',' or ']' in JSON array.");
      }
    }
    return JsonValue::make_array(std::move(items));
  }

  JsonValue parse_object(int depth) {
    if (depth >= kMaxDepth) {
      throw std::runtime_error("JSON nesting depth limit exceeded.");
    }
    take();
    skip_ws();
    std::vector<std::pair<std::string, JsonValue>> fields;
    if (pos_ < src_.size() && src_[pos_] == '}') {
      ++pos_;
      return JsonValue::make_object(std::move(fields));
    }
    for (;;) {
      skip_ws();
      std::string key = parse_string();
      skip_ws();
      if (take() != ':') {
        throw std::runtime_error("Expected ':' after JSON object key.");
      }
      skip_ws();
      JsonValue val = parse_value(depth + 1);
      fields.emplace_back(std::move(key), std::move(val));
      skip_ws();
      const char ch = take();
      if (ch == '}') {
        break;
      }
      if (ch != ',') {
        throw std::runtime_error("Expected ',' or '}' in JSON object.");
      }
    }
    return JsonValue::make_object(std::move(fields));
  }

  JsonValue parse_value(int depth) {
    skip_ws();
    const char ch = peek();
    if (ch == '"') {
      return JsonValue::make_string(parse_string());
    }
    if (ch == '{') {
      return parse_object(depth);
    }
    if (ch == '[') {
      return parse_array(depth);
    }
    if (ch == 't') {
      expect_literal("true");
      return JsonValue::make_bool(true);
    }
    if (ch == 'f') {
      expect_literal("false");
      return JsonValue::make_bool(false);
    }
    if (ch == 'n') {
      expect_literal("null");
      return JsonValue::make_null();
    }
    if (ch == '-' || (ch >= '0' && ch <= '9')) {
      return parse_number();
    }
    throw std::runtime_error("Unexpected token in JSON input.");
  }
};

inline JsonValue parse_json(const std::string& input) {
  JsonParser parser(input);
  return parser.parse();
}

inline std::string json_escape(const std::string& s) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(s.size() + 8U);
  out.push_back('"');
  for (unsigned char ch : s) {
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (ch < 0x20U) {
          out += "\\u00";
          out.push_back(kHex[(ch >> 4U) & 0x0FU]);
          out.push_back(kHex[ch & 0x0FU]);
        } else {
          out.push_back(static_cast<char>(ch));
        }
        break;
    }
  }
  out.push_back('"');
  return out;
}

inline std::string dump_json(const JsonValue& v) {
  switch (v.type) {
    case JsonValue::Type::kNull:
      return "null";
    case JsonValue::Type::kBool:
      return v.bool_val ? "true" : "false";
    case JsonValue::Type::kNumber:
      if (v.is_int) {
        return std::to_string(v.int_val);
      } else {
        std::ostringstream oss;
        oss.imbue(std::locale::classic());
        oss << std::setprecision(15) << v.num_val;
        return oss.str();
      }
    case JsonValue::Type::kString:
      return json_escape(v.str_val);
    case JsonValue::Type::kArray: {
      std::string out = "[";
      for (std::size_t i = 0U; i < v.arr_val.size(); ++i) {
        if (i > 0U) out.push_back(',');
        out += dump_json(v.arr_val[i]);
      }
      out.push_back(']');
      return out;
    }
    case JsonValue::Type::kObject: {
      std::string out = "{";
      for (std::size_t i = 0U; i < v.obj_val.size(); ++i) {
        if (i > 0U) out.push_back(',');
        out += json_escape(v.obj_val[i].first);
        out.push_back(':');
        out += dump_json(v.obj_val[i].second);
      }
      out.push_back('}');
      return out;
    }
  }
  return "null";
}

inline std::vector<std::uint8_t> base64_decode(const std::string& b64) {
  auto decode_char = [](unsigned char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };

  std::vector<std::uint8_t> out;
  out.reserve((b64.size() * 3U) / 4U);
  std::uint32_t accum = 0U;
  int bits = -8;
  for (unsigned char c : b64) {
    if (c == '=' || std::isspace(c)) {
      if (c == '=') {
        break;
      }
      continue;
    }
    const int d = decode_char(c);
    if (d < 0) {
      throw std::runtime_error("Invalid character in Base64 payload.");
    }
    accum = (accum << 6U) | static_cast<std::uint32_t>(d);
    bits += 6;
    if (bits >= 0) {
      out.push_back(static_cast<std::uint8_t>((accum >> static_cast<unsigned>(bits)) & 0xFFU));
      bits -= 8;
    }
  }
  return out;
}

}  // namespace cdp
}  // namespace winrpa

#endif  // WINRPA_CDP_JSON_H

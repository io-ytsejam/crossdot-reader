#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstring>
#include <string_view>

class CloudSyncResponse {
 public:
  static constexpr size_t MAX_BODY_BYTES = 4096;
  void append(const char* data, size_t length) {
    if (invalid) return;
    if (length > MAX_BODY_BYTES - size || (length && !data)) {
      invalid = true;
      return;
    }
    if (length) std::memcpy(body + size, data, length);
    size += length;
  }
  bool confirmsIngest() const {
    if (invalid || !size || !hasJsonTokens()) return false;
    // ArduinoJson stops at the closing brace. Track consumption so a valid
    // prefix followed by another document, garbage or NUL cannot confirm sync.
    Reader reader{body, size};
    // The response cap also bounds the parsed document. ArduinoJson uses
    // fallible heap allocations instead of placing its tree on the task stack.
    JsonDocument document;
    if (deserializeJson(document, reader) || !document.is<JsonObject>()) return false;
    while (reader.position < size) {
      const char c = body[reader.position++];
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
    }
    return document["ok"].is<bool>() && document["ok"].as<bool>();
  }

 private:
  static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
  static bool isDigit(char c) { return c >= '0' && c <= '9'; }

  static bool isJsonNumber(std::string_view token) {
    size_t p = 0;
    if (p < token.size() && token[p] == '-') ++p;
    if (p == token.size() || !isDigit(token[p])) return false;
    if (token[p] == '0') {
      ++p;
    } else {
      while (p < token.size() && isDigit(token[p])) ++p;
    }
    if (p < token.size() && token[p] == '.') {
      ++p;
      const size_t start = p;
      while (p < token.size() && isDigit(token[p])) ++p;
      if (p == start) return false;
    }
    if (p < token.size() && (token[p] == 'e' || token[p] == 'E')) {
      ++p;
      if (p < token.size() && (token[p] == '+' || token[p] == '-')) ++p;
      const size_t start = p;
      while (p < token.size() && isDigit(token[p])) ++p;
      if (p == start) return false;
    }
    return p == token.size();
  }

  // ArduinoJson deliberately accepts JavaScript extensions (bare keys, single
  // quotes, relaxed numbers, raw string controls). Reject those lexical forms;
  // leave structural parsing and escape decoding to ArduinoJson below.
  bool hasJsonTokens() const {
    bool quoted = false;
    size_t p = 0;
    while (p < size) {
      const char c = body[p++];
      if (isSpace(c)) continue;
      if (c == '"') {
        bool closed = false;
        while (p < size) {
          const unsigned char next = body[p++];
          if (next < 0x20) return false;
          if (next == '"') {
            closed = true;
            break;
          }
          if (next == '\\') {
            if (p == size || static_cast<unsigned char>(body[p]) < 0x20) return false;
            ++p;  // The JSON parser validates the escape itself.
          }
        }
        if (!closed) return false;
        quoted = true;
      } else if (c == ':') {
        if (!quoted) return false;
        quoted = false;
      } else if (c == '{' || c == '}' || c == '[' || c == ']' || c == ',') {
        quoted = false;
      } else {
        const size_t start = p - 1;
        while (p < size && !isSpace(body[p]) && body[p] != ',' && body[p] != '}' && body[p] != ']' && body[p] != ':' &&
               body[p] != '{' && body[p] != '[' && body[p] != '"')
          ++p;
        const std::string_view token(body + start, p - start);
        if (token != "true" && token != "false" && token != "null" && !isJsonNumber(token)) return false;
        quoted = false;
      }
    }
    return true;
  }

  struct Reader {
    const char* data;
    size_t length;
    size_t position = 0;
    int read() { return position < length ? static_cast<unsigned char>(data[position++]) : -1; }
  };
  char body[MAX_BODY_BYTES] = {};
  size_t size = 0;
  bool invalid = false;
};

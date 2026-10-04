#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

using std::min;

class String {
 public:
  String() = default;
  String(const char* value) : value_(value ? value : "") {}
  String(const std::string& value) : value_(value) {}
  const char* c_str() const { return value_.c_str(); }
  String(int value) : value_(std::to_string(value)) {}
  String(uint32_t value) : value_(std::to_string(value)) {}

  unsigned length() const { return static_cast<unsigned>(value_.size()); }
  bool isEmpty() const { return value_.empty(); }
  char operator[](unsigned index) const { return value_[index]; }
  int lastIndexOf(char needle, int from) const {
    if (value_.empty() || from < 0) return -1;
    const auto position = value_.rfind(needle, static_cast<size_t>(from));
    return position == std::string::npos ? -1 : static_cast<int>(position);
  }
  String substring(unsigned from) const {
    return from >= value_.size() ? String() : String(value_.substr(from));
  }
  String substring(unsigned from, unsigned to) const {
    if (from >= value_.size() || to <= from) return String();
    return String(value_.substr(from, to - from));
  }
  const std::string& str() const { return value_; }
  String& operator=(const char* value) { value_ = value ? value : ""; return *this; }
  String& operator+=(const String& other) { value_ += other.value_; return *this; }
  friend String operator+(const String& left, const String& right) {
    return String(left.value_ + right.value_);
  }
  friend String operator+(const String& left, const char* right) {
    return String(left.value_ + (right ? right : ""));
  }
  friend String operator+(const char* left, const String& right) {
    return String((left ? left : "") + right.value_);
  }
  friend bool operator==(const String& left, const char* right) {
    return left.value_ == (right ? right : "");
  }
  friend bool operator==(const String& left, const String& right) {
    return left.value_ == right.value_;
  }

 private:
  std::string value_;
};

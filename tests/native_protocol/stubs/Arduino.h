#pragma once

#include <cstdint>
#include <string>
#include <utility>

class String {
 public:
  String() = default;
  String(const char* value) : value_(value ? value : "") {}
  String(const std::string& value) : value_(value) {}
  String(std::string&& value) : value_(std::move(value)) {}
  String(int value) : value_(std::to_string(value)) {}
  String(uint32_t value) : value_(std::to_string(value)) {}

  unsigned length() const { return static_cast<unsigned>(value_.size()); }
  bool isEmpty() const { return value_.empty(); }
  char operator[](unsigned index) const { return value_[index]; }
  const char* c_str() const { return value_.c_str(); }
  const std::string& str() const { return value_; }
  void reserve(unsigned size) { value_.reserve(size); }
  String substring(unsigned from, unsigned to) const {
    if (from >= value_.size() || to <= from) return String();
    return String(value_.substr(from, to - from));
  }
  String& operator=(const char* value) {
    value_ = value ? value : "";
    return *this;
  }
  String& operator+=(char value) { value_ += value; return *this; }
  String& operator+=(const String& value) { value_ += value.value_; return *this; }

 private:
  std::string value_;
};

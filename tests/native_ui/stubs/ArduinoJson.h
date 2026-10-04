#pragma once

#include <map>
#include <string>
#include "Arduino.h"

class JsonDocument {
 public:
  struct Value {
    std::string text;
    bool number = false;
  };
  class Slot {
   public:
    Slot(JsonDocument& owner, const char* key) : owner_(owner), key_(key) {}
    Slot& operator=(const String& value) { owner_.set(key_, value.str()); return *this; }
    Slot& operator=(const char* value) { owner_.set(key_, value ? value : ""); return *this; }
    Slot& operator=(int value) { owner_.setNumber(key_, std::to_string(value)); return *this; }
   private:
    JsonDocument& owner_;
    std::string key_;
  };

  Slot operator[](const char* key) { return Slot(*this, key); }
  void set(const std::string& key, const std::string& value) { values_[key] = {value, false}; }
  void setNumber(const std::string& key, const std::string& value) { values_[key] = {value, true}; }
  const std::map<std::string, Value>& values() const { return values_; }

 private:
  std::map<std::string, Value> values_;
};

inline void serializeJson(const JsonDocument& document, String& output) {
  output = "{";
  bool first = true;
  for (const auto& value : document.values()) {
    if (!first) output += ",";
    first = false;
    output += String("\"") + value.first.c_str() + "\":";
    if (value.second.number) {
      output += value.second.text.c_str();
    } else {
      output += "\"";
      for (char character : value.second.text) {
        if (character == '"' || character == '\\') output += "\\";
        output += String(std::string(1, character));
      }
      output += "\"";
    }
  }
  output += "}";
}

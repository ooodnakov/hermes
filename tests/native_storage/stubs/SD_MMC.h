#pragma once
#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>

inline constexpr const char* FILE_READ = "r";
inline constexpr const char* FILE_WRITE = "w";

namespace fake_sd {
struct Faults {
  bool mount = false;
  int renameCall = 0;
  int failRenameAt = 0;
  size_t maxRead = static_cast<size_t>(-1);
};
inline std::map<std::string, std::string> files;
inline std::set<std::string> dirs{"/"};
inline Faults faults;
inline void reset() { files.clear(); dirs = {"/"}; faults = {}; }
inline void mkdirs(const std::string& path) {
  size_t pos = 1;
  while ((pos = path.find('/', pos)) != std::string::npos) {
    dirs.insert(path.substr(0, pos)); ++pos;
  }
  dirs.insert(path);
}
}

class File : public Stream {
 public:
  File() = default;
  File(std::string path, bool writing) : path_(std::move(path)), writing_(writing), open_(true) {
    if (writing_) fake_sd::files[path_].clear();
  }
  explicit operator bool() const { return open_; }
  size_t size() const { auto it = fake_sd::files.find(path_); return it == fake_sd::files.end() ? 0 : it->second.size(); }
  int read() override { char c; return readBytes(&c, 1) == 1 ? static_cast<unsigned char>(c) : -1; }
  size_t readBytes(char* buffer, size_t length) override {
    if (!open_ || writing_) return 0;
    auto& data = fake_sd::files[path_];
    size_t amount = std::min(length, data.size() - std::min(position_, data.size()));
    amount = std::min(amount, fake_sd::faults.maxRead);
    if (amount) std::memcpy(buffer, data.data() + position_, amount);
    position_ += amount;
    return amount;
  }
  size_t read(uint8_t* buffer, size_t length) {
    return readBytes(reinterpret_cast<char*>(buffer), length);
  }
  bool seek(uint32_t position) { if (!open_ || position > size()) return false; position_ = position; return true; }
  void flush() {}
  void close() { open_ = false; }
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* data, size_t length) override {
    if (!open_ || !writing_) return 0;
    fake_sd::files[path_].append(reinterpret_cast<const char*>(data), length);
    position_ += length;
    return length;
  }
 private:
  std::string path_;
  size_t position_ = 0;
  bool writing_ = false;
  bool open_ = false;
};

class FakeSdMmc {
 public:
  bool setPins(int, int, int) { return true; }
  bool begin(const char*, bool, bool) { return !fake_sd::faults.mount; }
  bool exists(const char* path) const { return fake_sd::files.count(path) || fake_sd::dirs.count(path); }
  bool mkdir(const char* path) { fake_sd::mkdirs(path); return true; }
  bool remove(const char* path) { fake_sd::files.erase(path); return true; }
  bool rename(const char* from, const char* to) {
    const int call = ++fake_sd::faults.renameCall;
    if (call == fake_sd::faults.failRenameAt) return false;
    auto it = fake_sd::files.find(from);
    if (it == fake_sd::files.end()) return false;
    fake_sd::files[to] = std::move(it->second);
    fake_sd::files.erase(it);
    return true;
  }
  File open(const char* path, const char* mode) {
    const bool writing = std::string(mode) == FILE_WRITE;
    if (!writing && !fake_sd::files.count(path)) return {};
    return File(path, writing);
  }
};
inline FakeSdMmc SD_MMC;

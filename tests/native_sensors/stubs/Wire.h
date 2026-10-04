#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <vector>

class TwoWire {
 public:
  void beginTransmission(uint8_t address) {
    address_ = address;
    tx_.clear();
  }
  size_t write(uint8_t value) {
    tx_.push_back(value);
    return writeLimit_ >= 0 && tx_.size() > static_cast<size_t>(writeLimit_) ? 0 : 1;
  }
  size_t write(const uint8_t* values, size_t length) {
    size_t count = 0;
    for (; count < length; ++count) {
      if (write(values[count]) != 1) break;
    }
    return count;
  }
  uint8_t endTransmission(bool stop) {
    ++transactionCount;
    if (failTransaction == transactionCount || failTransactions.count(transactionCount)) return 4;
    if (tx_.empty()) return 0;
    pointer_[address_] = tx_[0];
    if (stop && tx_.size() > 1) {
      if (address_ == 0x51) ++rtcWriteTransactions;
      for (size_t i = 1; i < tx_.size(); ++i) registers_[address_][tx_[0] + i - 1] = tx_[i];
    }
    return 0;
  }
  size_t requestFrom(uint8_t address, size_t length, bool) {
    rx_.clear();
    ++requestCount;
    const size_t availableLength = shortReadRequest == requestCount && length > 0
        ? length - 1 : length;
    for (size_t i = 0; i < availableLength; ++i) rx_.push_back(registers_[address][pointer_[address]++]);
    return availableLength;
  }
  int available() const { return static_cast<int>(rx_.size()); }
  int read() {
    if (rx_.empty()) return -1;
    const int value = rx_.front();
    rx_.pop_front();
    return value;
  }

  void set(uint8_t address, uint8_t reg, uint8_t value) { registers_[address][reg] = value; }
  uint8_t get(uint8_t address, uint8_t reg) const {
    const auto device = registers_.find(address);
    if (device == registers_.end()) return 0;
    const auto value = device->second.find(reg);
    return value == device->second.end() ? 0 : value->second;
  }
  int transactionCount = 0;
  int failTransaction = -1;
  std::set<int> failTransactions;
  int requestCount = 0;
  int shortReadRequest = -1;
  int writeLimit_ = -1;
  int rtcWriteTransactions = 0;

 private:
  uint8_t address_ = 0;
  std::vector<uint8_t> tx_;
  std::deque<uint8_t> rx_;
  std::map<uint8_t, uint8_t> pointer_;
  std::map<uint8_t, std::map<uint8_t, uint8_t>> registers_;
};

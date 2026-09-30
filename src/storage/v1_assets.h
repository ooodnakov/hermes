#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LovyanGFX.hpp>
#include <stddef.h>
#include <stdint.h>

namespace storage {

constexpr uint16_t kCharacterWidth = 144;
constexpr uint16_t kCharacterHeight = 144;
constexpr size_t kCharacterPixels = static_cast<size_t>(kCharacterWidth) * kCharacterHeight;
constexpr size_t kCharacterRgb565Bytes = kCharacterPixels * sizeof(uint16_t);

enum class AssetSource : uint8_t { None, Sd, CompiledFallback };
enum class AssetError : uint8_t {
  None,
  SdMount,
  ConfigMissing,
  ConfigTooLarge,
  ConfigParse,
  PackMismatch,
  MoodMissing,
  FrameMissing,
  FrameLength,
  FrameRead,
  PaletteIndex,
  ConfigWrite,
  ConfigRename,
};

struct FrameInfo {
  AssetSource source = AssetSource::None;
  uint16_t frameCount = 0;
  uint16_t frameMs = 180;
  AssetError sdError = AssetError::None;
};

// V1-only storage. No GPIO21 enable and no format-on-mount behavior.
class V1Assets {
 public:
  bool begin();
  bool sdReady() const { return sdReady_; }
  AssetError lastError() const { return lastError_; }
  uint16_t errorCount() const { return errorCount_; }

  // Writes one 144x144 character image into caller-owned RGB565 storage.
  // frameIndex wraps within the configured animation. On any unavailable or
  // invalid SD frame, the matching generated character frame is used.
  bool loadMoodFrame(const char* mood, uint16_t frameIndex, uint16_t* output,
                     size_t outputPixels, FrameInfo* info = nullptr);
  bool drawMoodFrame(const char* mood, uint16_t frameIndex, LGFX_Sprite& canvas,
                     int16_t x, int16_t y, uint16_t* scratch,
                     size_t scratchPixels, FrameInfo* info = nullptr);

  // Merge the supplied top-level sections into the V1 config, preserving all
  // unrelated JSON keys. Writes via a temporary file and FAT rename sequence.
  bool mergeConfiguration(JsonObjectConst updates);

 private:
  bool readSdFrame(const char* mood, uint16_t frameIndex, uint16_t* output,
                   size_t outputPixels, FrameInfo* info);
  bool readPackConfig(JsonDocument& doc, AssetError* error) const;
  bool writeJsonAtomically(const JsonDocument& doc);
  void record(AssetError error);

  bool sdReady_ = false;
  AssetError lastError_ = AssetError::None;
  uint16_t errorCount_ = 0;
};

uint16_t paletteColor(uint8_t index);
const char* errorName(AssetError error);

}  // namespace storage

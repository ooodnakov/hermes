#include "v1_assets.h"

#include <SD_MMC.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../boards/v1/board_config.h"
#include "v1_fallback_frames.h"

namespace storage {
namespace {
constexpr char kPackRoot[] = "/hermes-buddy-349-v1";
constexpr char kConfigPath[] = "/hermes-buddy-349-v1/config.json";
constexpr char kConfigTemp[] = "/hermes-buddy-349-v1/config.json.tmp";
constexpr char kConfigBackup[] = "/hermes-buddy-349-v1/config.json.bak";
constexpr uint32_t kPanelWidth = 640;
constexpr uint32_t kPanelHeight = 172;
constexpr uint32_t kArtworkX = 0;
constexpr uint32_t kArtworkY = 24;
constexpr size_t kPanelRowBytes = kPanelWidth / 2;
constexpr size_t kArtworkRowBytes = kCharacterWidth / 2;
constexpr size_t kPanelRawBytes = kPanelWidth * kPanelHeight / 2;
constexpr size_t kMaxConfigBytes = 8192;
constexpr uint16_t kMaxAnimationFrames = 32;
constexpr uint16_t kMaxFrameMs = 10000;
constexpr uint16_t kColors[] = {0x0000, 0x00C0, 0x03E0, 0x57EA};

bool equals(const char* a, const char* b) {
  return a && b && std::strcmp(a, b) == 0;
}

bool knownMood(const char* mood) {
  static constexpr const char* kMoods[] = {
      "idle", "blink", "wink", "smile", "happy", "sleep", "thinking", "waiting"};
  for (const char* known : kMoods) {
    if (equals(mood, known)) return true;
  }
  return false;
}

bool paletteMatches(JsonArrayConst palette) {
  if (palette.size() != sizeof(kColors) / sizeof(kColors[0])) return false;
  for (size_t i = 0; i < palette.size(); ++i) {
    const char* value = palette[i].as<const char*>();
    if (!value || value[0] != '0' || (value[1] != 'x' && value[1] != 'X')) return false;
    char* end = nullptr;
    const unsigned long color = std::strtoul(value + 2, &end, 16);
    if (!end || *end != '\0' || color != kColors[i]) return false;
  }
  return true;
}

const fallback::Frame* findFallback(const char* mood, uint16_t requested,
                                    uint16_t* frameCount) {
  const char* selectedMood = (mood && mood[0]) ? mood : "idle";
  uint16_t count = 0;
  for (size_t i = 0; i < fallback::kFrameCount; ++i) {
    if (equals(fallback::kFrames[i].mood, selectedMood)) ++count;
  }
  if (!count && !equals(selectedMood, "idle")) {
    selectedMood = "idle";
    for (size_t i = 0; i < fallback::kFrameCount; ++i) {
      if (equals(fallback::kFrames[i].mood, selectedMood)) ++count;
    }
  }
  if (frameCount) *frameCount = count;
  if (!count) return nullptr;
  const uint16_t selected = requested % count;
  uint16_t index = 0;
  for (size_t i = 0; i < fallback::kFrameCount; ++i) {
    if (!equals(fallback::kFrames[i].mood, selectedMood)) continue;
    if (index++ == selected) return &fallback::kFrames[i];
  }
  return nullptr;
}

bool unpackCharacterRow(const uint8_t* packed, uint16_t* destination,
                        uint8_t paletteSize) {
  for (size_t x = 0; x < kCharacterWidth; ++x) {
    const uint8_t byte = packed[x / 2];
    const uint8_t index = (x & 1U) ? (byte & 0x0F) : (byte >> 4);
    if (index >= paletteSize || index >= sizeof(kColors) / sizeof(kColors[0])) return false;
    destination[x] = kColors[index];
  }
  return true;
}
}  // namespace

uint16_t paletteColor(uint8_t index) {
  return index < sizeof(kColors) / sizeof(kColors[0]) ? kColors[index] : kColors[0];
}

const char* errorName(AssetError error) {
  switch (error) {
    case AssetError::None: return "none";
    case AssetError::SdMount: return "sd-mount";
    case AssetError::ConfigMissing: return "config-missing";
    case AssetError::ConfigTooLarge: return "config-too-large";
    case AssetError::ConfigParse: return "config-parse";
    case AssetError::PackMismatch: return "pack-mismatch";
    case AssetError::MoodMissing: return "mood-missing";
    case AssetError::FrameMissing: return "frame-missing";
    case AssetError::FrameLength: return "frame-length";
    case AssetError::FrameRead: return "frame-read";
    case AssetError::PaletteIndex: return "palette-index";
    case AssetError::ConfigWrite: return "config-write";
    case AssetError::ConfigRename: return "config-rename";
  }
  return "unknown";
}

void V1Assets::record(AssetError error) {
  lastError_ = error;
  if (error != AssetError::None && errorCount_ != UINT16_MAX) ++errorCount_;
}

bool V1Assets::begin() {
  if (sdReady_) return true;
  if (!SD_MMC.setPins(board::kSdClock, board::kSdCommand, board::kSdData0) ||
      !SD_MMC.begin("/sdcard", true, false)) {
    record(AssetError::SdMount);
    return false;
  }
  sdReady_ = true;
  // Recover a completed backup if power failed between the two FAT renames.
  if (!SD_MMC.exists(kConfigPath) && SD_MMC.exists(kConfigBackup)) {
    if (!SD_MMC.rename(kConfigBackup, kConfigPath)) {
      record(AssetError::ConfigRename);
      return true;
    }
  }
  record(AssetError::None);
  return true;
}

bool V1Assets::readPackConfig(JsonDocument& doc, AssetError* error) const {
  if (error) *error = AssetError::ConfigMissing;
  if (!sdReady_) {
    if (error) *error = AssetError::SdMount;
    return false;
  }
  if (!SD_MMC.exists(kConfigPath)) return false;
  File file = SD_MMC.open(kConfigPath, FILE_READ);
  if (!file) return false;
  if (file.size() > kMaxConfigBytes) {
    file.close();
    if (error) *error = AssetError::ConfigTooLarge;
    return false;
  }
  const DeserializationError parseError = deserializeJson(doc, file);
  file.close();
  if (parseError) {
    if (error) *error = AssetError::ConfigParse;
    return false;
  }
  const JsonObjectConst region = doc["character_region"].as<JsonObjectConst>();
  const bool valid = equals(doc["profile"], "349-v1") &&
      equals(doc["format"], "raw4-indexed-640x172") &&
      doc["width"] == kPanelWidth && doc["height"] == kPanelHeight &&
      doc["palette_size"] == 4 && region["x"] == kArtworkX && region["y"] == kArtworkY &&
      region["width"] == kCharacterWidth && region["height"] == kCharacterHeight &&
      paletteMatches(doc["palette_rgb565"].as<JsonArrayConst>());
  if (!valid) {
    if (error) *error = AssetError::PackMismatch;
    return false;
  }
  if (error) *error = AssetError::None;
  return true;
}

bool V1Assets::readSdFrame(const char* mood, uint16_t frameIndex, uint16_t* output,
                           size_t outputPixels, FrameInfo* info) {
  AssetError configError = AssetError::None;
  JsonDocument config;
  if (!readPackConfig(config, &configError)) {
    if (info) info->sdError = configError;
    record(configError);
    return false;
  }
  const char* selectedMood = (mood && knownMood(mood)) ? mood : "idle";
  const JsonObjectConst animation = config["animations"][selectedMood].as<JsonObjectConst>();
  const JsonArrayConst frames = animation["frames"].as<JsonArrayConst>();
  const uint16_t frameCount = static_cast<uint16_t>(frames.size());
  if (frameCount == 0 || frameCount > kMaxAnimationFrames) {
    if (info) info->sdError = AssetError::MoodMissing;
    record(AssetError::MoodMissing);
    return false;
  }
  const uint16_t frameMs = animation["frame_ms"] | 180;
  if (frameMs == 0 || frameMs > kMaxFrameMs) {
    if (info) info->sdError = AssetError::PackMismatch;
    record(AssetError::PackMismatch);
    return false;
  }
  char fileName[16];
  std::snprintf(fileName, sizeof(fileName), "%03u.raw4", frameIndex % frameCount);
  bool listed = false;
  for (JsonVariantConst item : frames) {
    const char* name = item.as<const char*>();
    if (equals(name, fileName)) { listed = true; break; }
  }
  if (!listed) {
    if (info) info->sdError = AssetError::PackMismatch;
    record(AssetError::PackMismatch);
    return false;
  }
  char path[96];
  const int pathLength = std::snprintf(path, sizeof(path), "%s/frames/%s/%s", kPackRoot, selectedMood, fileName);
  if (pathLength <= 0 || static_cast<size_t>(pathLength) >= sizeof(path)) {
    if (info) info->sdError = AssetError::FrameMissing;
    record(AssetError::FrameMissing);
    return false;
  }
  File file = SD_MMC.open(path, FILE_READ);
  if (!file) {
    if (info) info->sdError = AssetError::FrameMissing;
    record(AssetError::FrameMissing);
    return false;
  }
  if (file.size() != kPanelRawBytes) {
    file.close();
    if (info) info->sdError = AssetError::FrameLength;
    record(AssetError::FrameLength);
    return false;
  }
  // Validate every packed pixel, including the live-UI area, against the
  // palette advertised by this pack before decoding any artwork rows.
  uint8_t validationChunk[512];
  size_t validated = 0;
  while (validated < kPanelRawBytes) {
    const size_t amount = (kPanelRawBytes - validated < sizeof(validationChunk))
        ? (kPanelRawBytes - validated) : sizeof(validationChunk);
    const size_t received = file.read(validationChunk, amount);
    if (received != amount) {
      file.close();
      if (info) info->sdError = AssetError::FrameRead;
      record(AssetError::FrameRead);
      return false;
    }
    for (size_t i = 0; i < received; ++i) {
      if ((validationChunk[i] >> 4) >= 4 || (validationChunk[i] & 0x0F) >= 4) {
        file.close();
        if (info) info->sdError = AssetError::PaletteIndex;
        record(AssetError::PaletteIndex);
        return false;
      }
    }
    validated += received;
  }
  if (!output || outputPixels < kCharacterPixels) {
    file.close();
    if (info) info->sdError = AssetError::FrameRead;
    record(AssetError::FrameRead);
    return false;
  }
  uint8_t row[kArtworkRowBytes];
  for (uint16_t y = 0; y < kCharacterHeight; ++y) {
    const uint32_t offset = (kArtworkY + y) * kPanelRowBytes + kArtworkX / 2;
    if (!file.seek(offset) || file.read(row, sizeof(row)) != sizeof(row)) {
      file.close();
      if (info) info->sdError = AssetError::FrameRead;
      record(AssetError::FrameRead);
      return false;
    }
    if (!unpackCharacterRow(row, output + static_cast<size_t>(y) * kCharacterWidth, 4)) {
      file.close();
      if (info) info->sdError = AssetError::PaletteIndex;
      record(AssetError::PaletteIndex);
      return false;
    }
  }
  file.close();
  if (info) {
    info->source = AssetSource::Sd;
    info->frameCount = frameCount;
    info->frameMs = frameMs;
    info->sdError = AssetError::None;
  }
  record(AssetError::None);
  return true;
}

bool V1Assets::loadMoodFrame(const char* mood, uint16_t frameIndex, uint16_t* output,
                             size_t outputPixels, FrameInfo* info) {
  if (info) *info = FrameInfo{};
  if (!output || outputPixels < kCharacterPixels) {
    record(AssetError::FrameRead);
    if (info) info->sdError = AssetError::FrameRead;
    return false;
  }
  if (readSdFrame(mood, frameIndex, output, outputPixels, info)) return true;
  AssetError sdError = info ? info->sdError : lastError_;
  uint16_t frameCount = 0;
  const fallback::Frame* fallbackFrame = findFallback(mood, frameIndex, &frameCount);
  if (!fallbackFrame || frameCount == 0) {
    if (info) info->sdError = sdError;
    return false;
  }
  for (size_t i = 0; i < kCharacterPixels; ++i) {
    const uint8_t byte = fallbackFrame->data[i / 2];
    const uint8_t index = (i & 1U) ? (byte & 0x0F) : (byte >> 4);
    if (index >= 4) {
      record(AssetError::PaletteIndex);
      return false;
    }
    output[i] = kColors[index];
  }
  if (info) {
    info->source = AssetSource::CompiledFallback;
    info->frameCount = frameCount;
    info->frameMs = fallbackFrame->frameMs;
    info->sdError = sdError;
  }
  record(AssetError::None);
  return true;
}

bool V1Assets::drawMoodFrame(const char* mood, uint16_t frameIndex,
                             LGFX_Sprite& canvas, int16_t x, int16_t y,
                             uint16_t* scratch, size_t scratchPixels,
                             FrameInfo* info) {
  if (!scratch || scratchPixels < kCharacterPixels) {
    record(AssetError::FrameRead);
    if (info) {
      *info = FrameInfo{};
      info->sdError = AssetError::FrameRead;
    }
    return false;
  }
  if (!loadMoodFrame(mood, frameIndex, scratch, scratchPixels, info)) return false;
  canvas.pushImage(x, y, kCharacterWidth, kCharacterHeight, scratch);
  return true;
}

bool V1Assets::writeJsonAtomically(const JsonDocument& doc) {
  const size_t expected = measureJson(doc);
  if (expected == 0 || expected > kMaxConfigBytes) {
    record(AssetError::ConfigTooLarge);
    return false;
  }
  if (!SD_MMC.exists(kPackRoot) && !SD_MMC.mkdir(kPackRoot)) {
    record(AssetError::ConfigWrite);
    return false;
  }
  if (SD_MMC.exists(kConfigTemp)) SD_MMC.remove(kConfigTemp);
  File temp = SD_MMC.open(kConfigTemp, FILE_WRITE);
  if (!temp) { record(AssetError::ConfigWrite); return false; }
  const size_t written = serializeJson(doc, temp);
  temp.flush();
  temp.close();
  if (written == 0 || written != expected) {
    SD_MMC.remove(kConfigTemp);
    record(AssetError::ConfigWrite);
    return false;
  }
  JsonDocument check;
  File verify = SD_MMC.open(kConfigTemp, FILE_READ);
  const bool verified = verify && !deserializeJson(check, verify) && verify.size() == expected;
  if (verify) verify.close();
  if (!verified) {
    SD_MMC.remove(kConfigTemp);
    record(AssetError::ConfigWrite);
    return false;
  }
  const bool hadOriginal = SD_MMC.exists(kConfigPath);
  if (SD_MMC.exists(kConfigBackup)) SD_MMC.remove(kConfigBackup);
  if (hadOriginal && !SD_MMC.rename(kConfigPath, kConfigBackup)) {
    SD_MMC.remove(kConfigTemp);
    record(AssetError::ConfigRename);
    return false;
  }
  if (!SD_MMC.rename(kConfigTemp, kConfigPath)) {
    if (hadOriginal && SD_MMC.exists(kConfigBackup)) SD_MMC.rename(kConfigBackup, kConfigPath);
    SD_MMC.remove(kConfigTemp);
    record(AssetError::ConfigRename);
    return false;
  }
  if (SD_MMC.exists(kConfigBackup)) SD_MMC.remove(kConfigBackup);
  record(AssetError::None);
  return true;
}

bool V1Assets::mergeConfiguration(JsonObjectConst updates) {
  if (!sdReady_) { record(AssetError::SdMount); return false; }
  if (!SD_MMC.exists(kConfigPath) && SD_MMC.exists(kConfigBackup) &&
      !SD_MMC.rename(kConfigBackup, kConfigPath)) {
    record(AssetError::ConfigRename);
    return false;
  }
  JsonDocument config;
  if (SD_MMC.exists(kConfigPath)) {
    File existing = SD_MMC.open(kConfigPath, FILE_READ);
    if (!existing) { record(AssetError::ConfigMissing); return false; }
    if (existing.size() > kMaxConfigBytes) {
      existing.close();
      record(AssetError::ConfigTooLarge);
      return false;
    }
    const DeserializationError parseError = deserializeJson(config, existing);
    existing.close();
    if (parseError) { record(AssetError::ConfigParse); return false; }
  }
  for (JsonPairConst pair : updates) {
    JsonVariant target = config[pair.key()];
    if (pair.value().is<JsonObjectConst>()) {
      JsonObject section = target.is<JsonObject>() ? target.as<JsonObject>() : config[pair.key()].to<JsonObject>();
      for (JsonPairConst field : pair.value().as<JsonObjectConst>()) section[field.key()] = field.value();
    } else {
      target.set(pair.value());
    }
  }
  return writeJsonAtomically(config);
}

}  // namespace storage

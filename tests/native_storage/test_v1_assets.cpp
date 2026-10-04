#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <storage/v1_assets.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr char kRoot[] = "/hermes-buddy-349-v1";
constexpr char kConfig[] = "/hermes-buddy-349-v1/config.json";
constexpr char kBackup[] = "/hermes-buddy-349-v1/config.json.bak";
constexpr char kFrame[] = "/hermes-buddy-349-v1/frames/idle/000.raw4";
constexpr size_t kPanelRawBytes = 640U * 172U / 2U;
constexpr const char* kValidConfig =
    R"({"profile":"349-v1","format":"raw4-indexed-640x172","width":640,"height":172,"palette_size":4,"palette_rgb565":["0x0000","0x00C0","0x03E0","0x57EA"],"character_region":{"x":0,"y":24,"width":144,"height":144},"animations":{"idle":{"frame_ms":180,"frames":["000.raw4"]}}})";

void installPack(bool includeFrame = true) {
  fake_sd::reset();
  fake_sd::mkdirs(kRoot);
  fake_sd::files[kConfig] = kValidConfig;
  if (includeFrame) fake_sd::files[kFrame] = std::string(kPanelRawBytes, '\0');
}

void assertFallback(storage::V1Assets& assets, storage::AssetError expected) {
  std::vector<uint16_t> pixels(storage::kCharacterPixels);
  storage::FrameInfo info;
  assert(assets.loadMoodFrame("idle", 0, pixels.data(), pixels.size(), &info));
  assert(info.source == storage::AssetSource::CompiledFallback);
  assert(info.sdError == expected);
  assert(info.frameCount > 0);
}

void testAbsentAndCorruptConfigUseFallback() {
  fake_sd::reset();
  storage::V1Assets noCard;
  fake_sd::faults.mount = true;
  assert(!noCard.begin());
  assert(noCard.lastError() == storage::AssetError::SdMount);
  assertFallback(noCard, storage::AssetError::SdMount);

  fake_sd::reset();
  assert(noCard.begin());
  assertFallback(noCard, storage::AssetError::ConfigMissing);

  fake_sd::files[kConfig] = "{broken";
  assertFallback(noCard, storage::AssetError::ConfigParse);
}

void testFrameFailuresAndPaletteBoundsUseFallback() {
  installPack(false);
  storage::V1Assets assets;
  assert(assets.begin());
  assertFallback(assets, storage::AssetError::FrameMissing);

  fake_sd::files[kFrame] = std::string(kPanelRawBytes - 1, '\0');
  assertFallback(assets, storage::AssetError::FrameLength);

  fake_sd::files[kFrame] = std::string(kPanelRawBytes, '\0');
  fake_sd::files[kFrame].back() = static_cast<char>(0x44);  // invalid outside artwork region
  assertFallback(assets, storage::AssetError::PaletteIndex);

  fake_sd::files[kFrame] = std::string(kPanelRawBytes, '\0');
  fake_sd::faults.maxRead = 7;
  assertFallback(assets, storage::AssetError::FrameRead);
}

void testValidSdFrameDecodesAndWrapsIndex() {
  installPack();
  storage::V1Assets assets;
  assert(assets.begin());
  std::vector<uint16_t> pixels(storage::kCharacterPixels);
  storage::FrameInfo info;
  assert(assets.loadMoodFrame("idle", 42, pixels.data(), pixels.size(), &info));
  assert(info.source == storage::AssetSource::Sd);
  assert(info.frameCount == 1);
  assert(info.sdError == storage::AssetError::None);
  for (uint16_t pixel : pixels) assert(pixel == storage::paletteColor(0));
}

void testMergePreservesAssetAndUnrelatedConfiguration() {
  installPack();
  storage::V1Assets assets;
  assert(assets.begin());
  fake_sd::files[kConfig] =
      R"({"profile":"349-v1","animations":{"idle":{"frames":["000.raw4"]}},"wifi":{"ssid":"old","password":"secret"},"custom":{"keep":17},"display":{"brightness":20}})";
  JsonDocument updates;
  updates["wifi"]["ssid"] = "new-network";
  updates["host"]["name"] = "hermes";
  updates["display"]["brightness"] = 80;
  updates["display"]["rotation"] = 1;
  assert(assets.mergeConfiguration(updates.as<JsonObjectConst>()));
  JsonDocument saved;
  assert(!deserializeJson(saved, fake_sd::files[kConfig]));
  assert(saved["profile"] == "349-v1");
  assert(saved["animations"]["idle"]["frames"][0] == "000.raw4");
  assert(saved["wifi"]["ssid"] == "new-network");
  assert(saved["wifi"]["password"] == "secret");
  assert(saved["host"]["name"] == "hermes");
  assert(saved["custom"]["keep"] == 17);
  assert(saved["display"]["brightness"] == 80);
  assert(saved["display"]["rotation"] == 1);
}

void testAtomicRenameFailureRestoresOriginal() {
  fake_sd::reset();
  fake_sd::mkdirs(kRoot);
  fake_sd::files[kConfig] = R"({"keep":"original"})";
  storage::V1Assets assets;
  assert(assets.begin());
  JsonDocument updates;
  updates["host"] = "new";
  fake_sd::faults.failRenameAt = 2;  // temp -> final after original -> backup
  assert(!assets.mergeConfiguration(updates.as<JsonObjectConst>()));
  assert(assets.lastError() == storage::AssetError::ConfigRename);
  JsonDocument original;
  assert(!deserializeJson(original, fake_sd::files[kConfig]));
  assert(original["keep"] == "original");
  assert(!fake_sd::files.count(kBackup));
  assert(!fake_sd::files.count("/hermes-buddy-349-v1/config.json.tmp"));
}

void testBootRecoversBackupAndReportsFailedRecovery() {
  fake_sd::reset();
  fake_sd::mkdirs(kRoot);
  fake_sd::files[kBackup] = R"({"recovered":true})";
  storage::V1Assets recovered;
  assert(recovered.begin());
  assert(recovered.lastError() == storage::AssetError::None);
  assert(fake_sd::files.count(kConfig));
  assert(!fake_sd::files.count(kBackup));

  fake_sd::reset();
  fake_sd::mkdirs(kRoot);
  fake_sd::files[kBackup] = R"({"recovered":true})";
  fake_sd::faults.failRenameAt = 1;
  storage::V1Assets failed;
  assert(failed.begin());  // SD mounted even though config recovery failed.
  assert(failed.sdReady());
  assert(failed.lastError() == storage::AssetError::ConfigRename);
  assert(fake_sd::files.count(kBackup));
  assert(!fake_sd::files.count(kConfig));
}

void testProvisioningRetriesBackupRecoveryBeforeReplacingConfiguration() {
  fake_sd::reset();
  fake_sd::mkdirs(kRoot);
  fake_sd::files[kBackup] = R"({"profile":"349-v1","wifi":{"ssid":"prior"},"asset_metadata":{"keep":true}})";
  fake_sd::faults.failRenameAt = 1;
  storage::V1Assets retry;
  assert(retry.begin());
  assert(retry.lastError() == storage::AssetError::ConfigRename);
  JsonDocument updates;
  updates["wifi"]["ssid"] = "updated";
  assert(retry.mergeConfiguration(updates.as<JsonObjectConst>()));
  JsonDocument saved;
  assert(!deserializeJson(saved, fake_sd::files[kConfig]));
  assert(saved["profile"] == "349-v1");
  assert(saved["wifi"]["ssid"] == "updated");
  assert(saved["asset_metadata"]["keep"] == true);
  assert(!fake_sd::files.count(kBackup));

  fake_sd::reset();
  fake_sd::mkdirs(kRoot);
  fake_sd::files[kBackup] = R"({"keep":"backup"})";
  fake_sd::faults.failRenameAt = 1;
  storage::V1Assets unavailable;
  assert(unavailable.begin());
  fake_sd::faults.failRenameAt = 2;  // fail the provisioning retry too
  assert(!unavailable.mergeConfiguration(updates.as<JsonObjectConst>()));
  assert(unavailable.lastError() == storage::AssetError::ConfigRename);
  assert(fake_sd::files[kBackup] == R"({"keep":"backup"})");
  assert(!fake_sd::files.count(kConfig));
}
}

int main() {
  testAbsentAndCorruptConfigUseFallback();
  testFrameFailuresAndPaletteBoundsUseFallback();
  testValidSdFrameDecodesAndWrapsIndex();
  testMergePreservesAssetAndUnrelatedConfiguration();
  testAtomicRenameFailureRestoresOriginal();
  testBootRecoversBackupAndReportsFailedRecovery();
  testProvisioningRetriesBackupRecoveryBeforeReplacingConfiguration();
}

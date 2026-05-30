#include "settings/SettingsStore.h"

#include <Preferences.h>
#include <cstring>

namespace {

constexpr const char* kNamespace = "flir";

void copyString(char* dest, size_t destSize, const char* src) {
  if (destSize == 0) {
    return;
  }
  strncpy(dest, src, destSize - 1);
  dest[destSize - 1] = '\0';
}

}  // namespace

bool SettingsStore::begin() {
  return true;
}

AppSettings SettingsStore::load() {
  AppSettings settings = defaults();
  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) {
    return settings;
  }

  preferences.getString("savePath", settings.savePath, sizeof(settings.savePath));
  preferences.getString("locale", settings.locale, sizeof(settings.locale));
  preferences.getString("dateFmt", settings.dateFormat, sizeof(settings.dateFormat));
  preferences.getString("timeFmt", settings.timeFormat, sizeof(settings.timeFormat));
  settings.landscape = preferences.getBool("landscape", settings.landscape);
  settings.autoRotate = preferences.getBool("autoRot", settings.autoRotate);
  settings.inactivitySleepSeconds =
      preferences.getUShort("idleSleep", settings.inactivitySleepSeconds);
  settings.temperatureOffsetTenths =
      static_cast<int8_t>(preferences.getShort("tempOff10", settings.temperatureOffsetTenths));
  settings.includeFilenameInCapture =
      preferences.getBool("capName", settings.includeFilenameInCapture);
  settings.saveRawCapture =
      preferences.getBool("saveRaw", settings.saveRawCapture);
  settings.showHotColdDetails =
      preferences.getBool("showHiLo", settings.showHotColdDetails);
  settings.showCenterTemperature =
      preferences.getBool("showCtr", settings.showCenterTemperature);
  preferences.end();

  if (settings.savePath[0] != '/') {
    copyString(settings.savePath, sizeof(settings.savePath), "/flir");
  }
  if (settings.inactivitySleepSeconds > 3600) {
    settings.inactivitySleepSeconds = 0;
  }
  if (settings.temperatureOffsetTenths < -50 || settings.temperatureOffsetTenths > 50) {
    settings.temperatureOffsetTenths = 0;
  }
  return settings;
}

bool SettingsStore::save(const AppSettings& settings) {
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    return false;
  }

  preferences.putString("savePath", settings.savePath);
  preferences.putString("locale", settings.locale);
  preferences.putString("dateFmt", settings.dateFormat);
  preferences.putString("timeFmt", settings.timeFormat);
  preferences.putBool("landscape", settings.landscape);
  preferences.putBool("autoRot", settings.autoRotate);
  preferences.putUShort("idleSleep", settings.inactivitySleepSeconds);
  preferences.putShort("tempOff10", settings.temperatureOffsetTenths);
  preferences.putBool("capName", settings.includeFilenameInCapture);
  preferences.putBool("saveRaw", settings.saveRawCapture);
  preferences.putBool("showHiLo", settings.showHotColdDetails);
  preferences.putBool("showCtr", settings.showCenterTemperature);
  preferences.end();
  return true;
}

AppSettings SettingsStore::defaults() const {
  return AppSettings{};
}

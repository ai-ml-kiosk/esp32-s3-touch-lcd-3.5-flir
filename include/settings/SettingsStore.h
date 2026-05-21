#pragma once

#include "settings/AppSettings.h"

class SettingsStore {
 public:
  bool begin();
  AppSettings load();
  bool save(const AppSettings& settings);

 private:
  AppSettings defaults() const;
};

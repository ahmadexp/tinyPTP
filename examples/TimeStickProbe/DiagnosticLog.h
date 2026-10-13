#pragma once
#include "BuildConfig.h"

bool diagnosticsEnabled = TINYPTP_BOOT_ROLE == 0;
struct DiagnosticLog {
  template<class T> void print(T value) { if (diagnosticsEnabled) Serial.print(value); }
  template<class T> void print(T value, int base) { if (diagnosticsEnabled) Serial.print(value, base); }
  template<class T> void println(T value) { if (diagnosticsEnabled) Serial.println(value); }
  template<class T> void println(T value, int base) { if (diagnosticsEnabled) Serial.println(value, base); }
  void println() { if (diagnosticsEnabled) Serial.println(); }
} Diag;

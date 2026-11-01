#include "TimeTool.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "Secrets.h"

namespace {

struct CityTz { const char* name; const char* tz; };

// Deliberately a small, non-exhaustive set of major cities — POSIX TZ
// strings, DST-aware via libc, same mechanism as the default location.
// An unrecognized city fails cleanly (see fetch()) rather than guessing.
constexpr CityTz kCityTimezones[] = {
  {"new york",    "EST5EDT,M3.2.0,M11.1.0"},
  {"los angeles", "PST8PDT,M3.2.0,M11.1.0"},
  {"chicago",     "CST6CDT,M3.2.0,M11.1.0"},
  {"london",      "GMT0BST,M3.5.0/1,M10.5.0"},
  {"paris",       "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"berlin",      "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"tokyo",       "JST-9"},
  {"beijing",     "CST-8"},
  {"shanghai",    "CST-8"},
  {"moscow",      "MSK-3"},
  {"dubai",       "GST-4"},
  {"sydney",      "AEST-10AEDT,M10.1.0,M4.1.0/3"},
  {"mumbai",      "IST-5:30"},
  {"delhi",       "IST-5:30"},
  {"singapore",   "SGT-8"},
  {"hong kong",   "HKT-8"},
  {"seoul",       "KST-9"},
  {"toronto",     "EST5EDT,M3.2.0,M11.1.0"},
  {"sao paulo",   "BRT3"},
  {"mexico city", "CST6"},
};
constexpr size_t kCityTimezoneCount = sizeof(kCityTimezones) / sizeof(kCityTimezones[0]);

const char* kWeekdayNames[7] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

// Renders `now` as of `posixTz` without disturbing the process-wide TZ
// the rest of the firmware (and the default-location fast path) relies
// on — swap in, read, swap back.
struct tm localTimeInTz(time_t now, const char* posixTz) {
  char prevTz[64] = {0};
  const char* existing = getenv("TZ");
  if (existing) strncpy(prevTz, existing, sizeof(prevTz) - 1);

  setenv("TZ", posixTz, 1);
  tzset();
  struct tm result;
  localtime_r(&now, &result);

  if (prevTz[0]) setenv("TZ", prevTz, 1);
  else unsetenv("TZ");
  tzset();
  return result;
}

const char* findCityTz(const String& location) {
  String needle = location;
  needle.trim();
  needle.toLowerCase();
  for (size_t i = 0; i < kCityTimezoneCount; i++) {
    if (needle == kCityTimezones[i].name) return kCityTimezones[i].tz;
  }
  return nullptr;
}

} // namespace

void TimeTool::begin() {
  configTzTime(DEFAULT_LOCATION_TZ, "pool.ntp.org", "time.nist.gov");
}

ToolResult TimeTool::fetch(const String& location, const String& targetDateIso) {
  ToolResult result;

  time_t now = time(nullptr);
  if (now < 1700000000) {  // before 2023-11 => SNTP hasn't synced yet
    result.error = "clock not synced yet";
    return result;
  }

  String loc = location;
  loc.trim();
  const char* tz = DEFAULT_LOCATION_TZ;
  String placeName = DEFAULT_LOCATION_NAME;
  if (loc.length() > 0 && !loc.equalsIgnoreCase(DEFAULT_LOCATION_NAME)) {
    const char* cityTz = findCityTz(loc);
    if (!cityTz) {
      result.error = "don't know that city's timezone";
      return result;
    }
    tz = cityTz;
    placeName = loc;
  }

  struct tm t = localTimeInTz(now, tz);
  char buf[16];
  String data = "location=" + placeName;
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  data += ", date=" + String(buf);
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  data += ", time=" + String(buf);
  data += ", day=";
  data += kWeekdayNames[t.tm_wday];

  if (targetDateIso.length() == 10) {
    struct tm target = {};
    target.tm_year = targetDateIso.substring(0, 4).toInt() - 1900;
    target.tm_mon  = targetDateIso.substring(5, 7).toInt() - 1;
    target.tm_mday = targetDateIso.substring(8, 10).toInt();
    target.tm_hour = 12;  // noon — avoids DST-boundary edge cases in the day count
    time_t targetEpoch = mktime(&target);
    if (targetEpoch > 0) {
      double diffDays = difftime(targetEpoch, now) / 86400.0;
      data += ", days_until_" + targetDateIso + "=" + String((int)(diffDays + 0.5));
    }
  }

  result.success = true;
  result.data = data;
  return result;
}

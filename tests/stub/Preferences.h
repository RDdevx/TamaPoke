#pragma once
// An in-memory stand-in for the ESP32 Preferences/NVS API, good enough to run
// the real storage code (party.cpp, save.cpp) on a PC: values really persist
// between begin()/end() pairs within a run, and it copies the one NVS behaviour
// the migration code leans on -- getBytes() returns 0 and reads NOTHING when the
// destination is smaller than the stored value.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

struct PrefsStore {
  static std::map<std::string, std::vector<uint8_t>> &db() {
    static std::map<std::string, std::vector<uint8_t>> d;
    return d;
  }
};

class Preferences {
  std::string ns;
  template <class T> void putRaw(const char *k, T v) {
    std::vector<uint8_t> b(sizeof(T)); memcpy(b.data(), &v, sizeof(T));
    PrefsStore::db()[ns + "/" + k] = b;
  }
  template <class T> T getRaw(const char *k, T d) {
    auto it = PrefsStore::db().find(ns + "/" + k);
    if (it == PrefsStore::db().end() || it->second.size() != sizeof(T)) return d;
    T v; memcpy(&v, it->second.data(), sizeof(T)); return v;
  }
 public:
  bool begin(const char *name, bool = false) { ns = name; return true; }
  void end() {}
  void clear() {
    std::string pre = ns + "/";
    for (auto it = PrefsStore::db().begin(); it != PrefsStore::db().end();)
      it = (it->first.compare(0, pre.size(), pre) == 0) ? PrefsStore::db().erase(it) : std::next(it);
  }
  bool remove(const char *k) { return PrefsStore::db().erase(ns + "/" + k) > 0; }
  bool isKey(const char *k) { return PrefsStore::db().count(ns + "/" + k) > 0; }
  size_t getBytesLength(const char *k) {
    auto it = PrefsStore::db().find(ns + "/" + k);
    return it == PrefsStore::db().end() ? 0 : it->second.size();
  }
  size_t getBytes(const char *k, void *buf, size_t maxLen) {
    auto it = PrefsStore::db().find(ns + "/" + k);
    if (it == PrefsStore::db().end() || maxLen < it->second.size()) return 0;
    memcpy(buf, it->second.data(), it->second.size());
    return it->second.size();
  }
  size_t putBytes(const char *k, const void *buf, size_t n) {
    PrefsStore::db()[ns + "/" + k] = std::vector<uint8_t>((const uint8_t *)buf, (const uint8_t *)buf + n);
    return n;
  }
  uint8_t getUChar(const char *k, uint8_t d = 0) { return getRaw<uint8_t>(k, d); }
  void putUChar(const char *k, uint8_t v) { putRaw<uint8_t>(k, v); }
  int8_t getChar(const char *k, int8_t d = 0) { return getRaw<int8_t>(k, d); }
  void putChar(const char *k, int8_t v) { putRaw<int8_t>(k, v); }
  uint16_t getUShort(const char *k, uint16_t d = 0) { return getRaw<uint16_t>(k, d); }
  void putUShort(const char *k, uint16_t v) { putRaw<uint16_t>(k, v); }
  int16_t getShort(const char *k, int16_t d = 0) { return getRaw<int16_t>(k, d); }
  void putShort(const char *k, int16_t v) { putRaw<int16_t>(k, v); }
  uint32_t getUInt(const char *k, uint32_t d = 0) { return getRaw<uint32_t>(k, d); }
  void putUInt(const char *k, uint32_t v) { putRaw<uint32_t>(k, v); }
  int getInt(const char *k, int d = 0) { return getRaw<int>(k, d); }
  void putInt(const char *k, int v) { putRaw<int>(k, v); }
  bool getBool(const char *k, bool d = false) { return getRaw<uint8_t>(k, d ? 1 : 0) != 0; }
  void putBool(const char *k, bool v) { putRaw<uint8_t>(k, v ? 1 : 0); }
  size_t getString(const char *k, char *buf, size_t maxLen) {
    auto it = PrefsStore::db().find(ns + "/" + k);
    if (it == PrefsStore::db().end() || maxLen == 0) { if (maxLen) buf[0] = 0; return 0; }
    size_t n = it->second.size() < maxLen - 1 ? it->second.size() : maxLen - 1;
    memcpy(buf, it->second.data(), n); buf[n] = 0; return n;
  }
  size_t putString(const char *k, const char *s) { return putBytes(k, s, strlen(s)); }
};

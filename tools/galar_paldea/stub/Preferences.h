// Host-side mock of the ESP32 Preferences API. Backs onto a single in-memory
// map so save.cpp can be compiled and tested on the desktop.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

struct MockNvsEntry {
  std::string key;
  uint8_t kind;                     // 0=scalar raw, 1=bytes, 2=string
  std::vector<uint8_t> data;
};

// one namespace-wide store; save.cpp and the test share it (single TU)
extern std::vector<MockNvsEntry> g_nvs;

inline MockNvsEntry *nvsFind(const char *key) {
  for (auto &e : g_nvs) if (e.key == key) return &e;
  return nullptr;
}
inline void nvsPut(const char *key, uint8_t kind, const uint8_t *d, size_t n) {
  MockNvsEntry *e = nvsFind(key);
  if (!e) { g_nvs.push_back({key, kind, {}}); e = &g_nvs.back(); }
  e->kind = kind; e->data.assign(d, d + n);
}

class Preferences {
public:
  bool begin(const char * /*name*/, bool /*ro*/ = true) { return true; }
  void end() {}
  void clear() { g_nvs.clear(); }

  bool isKey(const char *key) { return nvsFind(key) != nullptr; }

  void putUChar(const char *k, uint8_t v) { nvsPut(k, 0, &v, 1); }
  uint8_t getUChar(const char *k, uint8_t d) {
    MockNvsEntry *e = nvsFind(k); return (e && e->data.size() == 1) ? e->data[0] : d;
  }
  void putChar(const char *k, int8_t v) { nvsPut(k, 0, (uint8_t *)&v, 1); }
  int8_t getChar(const char *k, int8_t d) {
    MockNvsEntry *e = nvsFind(k); return (e && e->data.size() == 1) ? (int8_t)e->data[0] : d;
  }
  void putBool(const char *k, bool v) { uint8_t b = v; nvsPut(k, 0, &b, 1); }
  bool getBool(const char *k, bool d) {
    MockNvsEntry *e = nvsFind(k); return (e && e->data.size() == 1) ? e->data[0] != 0 : d;
  }
  void putUShort(const char *k, uint16_t v) { nvsPut(k, 0, (uint8_t *)&v, 2); }
  uint16_t getUShort(const char *k, uint16_t d) {
    MockNvsEntry *e = nvsFind(k);
    return (e && e->data.size() == 2) ? *(uint16_t *)e->data.data() : d;
  }
  void putShort(const char *k, int16_t v) { nvsPut(k, 0, (uint8_t *)&v, 2); }
  int16_t getShort(const char *k, int16_t d) {
    MockNvsEntry *e = nvsFind(k);
    return (e && e->data.size() == 2) ? *(int16_t *)e->data.data() : d;
  }
  void putUInt(const char *k, uint32_t v) { nvsPut(k, 0, (uint8_t *)&v, 4); }
  uint32_t getUInt(const char *k, uint32_t d) {
    MockNvsEntry *e = nvsFind(k);
    return (e && e->data.size() == 4) ? *(uint32_t *)e->data.data() : d;
  }
  void putBytes(const char *k, const void *v, size_t n) {
    nvsPut(k, 1, (const uint8_t *)v, n);
  }
  size_t getBytesLength(const char *k) {
    MockNvsEntry *e = nvsFind(k); return e ? e->data.size() : 0;
  }
  size_t getBytes(const char *k, void *v, size_t n) {
    MockNvsEntry *e = nvsFind(k);
    if (!e) return 0;
    size_t c = e->data.size() < n ? e->data.size() : n;
    memcpy(v, e->data.data(), c);
    return e->data.size();
  }
  void putString(const char *k, const char *v) {
    nvsPut(k, 2, (const uint8_t *)v, strlen(v) + 1);
  }
  size_t getString(const char *k, char *v, size_t n) {
    MockNvsEntry *e = nvsFind(k);
    if (!e || e->kind != 2) { if (n) v[0] = 0; return 0; }
    size_t c = e->data.size() < n ? e->data.size() : n;
    memcpy(v, e->data.data(), c);
    if (c < n) v[c] = 0;
    return c ? strlen(v) : 0;
  }
};

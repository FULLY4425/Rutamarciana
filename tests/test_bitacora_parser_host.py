"""Compile the manifest JSON parser from bitacora.ino and exercise it on the host."""
import os
import subprocess
import tempfile
from pathlib import Path


def test_bitacora_manifest_parser_host():
    ino = (Path(__file__).resolve().parents[1] / 'firmware/psion_micor_stable/bitacora.ino').read_text()
    parser = ino[ino.index('struct Parser {'):ino.index('} parser(stream, length);') + 1]
    harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#define PSTR(x) x
const int BT_WMAX = 16;
uint32_t fakeMs = 0;
uint32_t millis() { return fakeMs; }
void delay(int) { fakeMs++; }
struct WiFiClient {
  std::string data; size_t pos = 0;
  int available() { return pos < data.size() ? 1 : 0; }
  int read() { return pos < data.size() ? (unsigned char)data[pos++] : -1; }
};
''' + parser + ''';
''' + r'''
static bool run(const std::string &data, int length, char names[][24], int &count, bool &found) {
  WiFiClient stream; stream.data = data;
  Parser p(&stream, length);
  bool ok = p.value(1, 0); p.ws();
  ok = ok && p.peek() == -1 && p.left == 0 && p.found && p.count > 0;
  count = p.count; found = p.found;
  for (int i = 0; i < count && i < BT_WMAX; ++i) { memcpy(names[i], p.names[i], 24); }
  return ok;
}
static bool ok(const std::string &d, char names[][24], int &count, bool &found) {
  return run(d, (int)d.size(), names, count, found);
}
template<int N> static bool bad(const char (&d)[N], char names[][24], int &count, bool &found) {
  return !ok(std::string(d, N - 1), names, count, found);
}
int main() {
  char names[BT_WMAX][24]; int count; bool found;
  // full manifest: extra keys, numbers, arrays, escapes
  assert(ok("{\"site\":\"jez\",\"waypoints\":[{\"id\":1,\"name\":\"START\",\"row\":2,\"col\":2},"
            "{\"id\":2,\"name\":\"S\\u0031\",\"row\":3,\"col\":4}],"
            "\"routes\":{\"safe\":{\"time_s\":14400.0,\"distance_m\":12,\"points\":[[2,2],[3,4]]}},"
            "\"budget_s\":14400,\"science_stops\":5}", names, count, found));
  assert(count == 2 && found && strcmp(names[0], "START") == 0 && strcmp(names[1], "S1") == 0);
  // trailing garbage rejected
  assert(bad("{\"waypoints\":[{\"name\":\"A\"}]} junk", names, count, found));
  // stream ends before the declared Content-Length: rejected
  assert(!run("{\"waypoints\":[{\"name\":\"A\"}]}", 40, names, count, found));
  // duplicate waypoint names rejected
  assert(bad("{\"waypoints\":[{\"name\":\"A\"},{\"name\":\"A\"}]}", names, count, found));
  // '|' or '=' or control chars in names rejected (file-format safety)
  assert(bad("{\"waypoints\":[{\"name\":\"a|b\"}]}", names, count, found));
  assert(bad("{\"waypoints\":[{\"name\":\"a=b\"}]}", names, count, found));
  assert(bad("{\"waypoints\":[{\"name\":\"a\\u0000b\"}]}", names, count, found));
  // names longer than the 24-char slot rejected
  assert(bad("{\"waypoints\":[{\"name\":\"0123456789012345678901234\"}]}", names, count, found));
  // empty waypoints rejected (the offline fallback covers it instead)
  assert(bad("{\"waypoints\":[]}", names, count, found));
  // waypoints key at the wrong nesting level is not the catalog
  assert(bad("{\"x\":{\"waypoints\":[{\"name\":\"A\"}]}}", names, count, found));
  // 15 names fit, 16 do not (the OTRO slot is reserved)
  std::string many = "{\"waypoints\":[";
  for (int i = 0; i < 15; ++i) many += (i ? "," : "") + std::string("{\"name\":\"W") + std::to_string(i) + "\"}";
  assert(ok(many + "]}", names, count, found) && count == 15);
  many += ",{\"name\":\"EXTRA\"}";
  assert(!ok(many + "]}", names, count, found));
  // non-string / missing name rejected
  assert(bad("{\"waypoints\":[{\"name\":7}]}", names, count, found));
  assert(bad("{\"waypoints\":[{\"id\":1}]}", names, count, found));
  // strict numbers: leading zero rejected
  assert(bad("{\"waypoints\":[{\"name\":\"A\"}],\"x\":01}", names, count, found));
}
'''
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as d:
        src = Path(d) / 'test.cpp'
        exe = Path(d) / 'test'
        src.write_text(harness)
        subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-o', str(exe), str(src)], check=True)
        subprocess.run([str(exe)], check=True)

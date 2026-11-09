#!/bin/sh
# Builds and runs the trade protocol test. Needs only g++ -- link.cpp is pure
# protocol, so the Arduino/Preferences stubs in tests/stub are enough.
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -O1 -Istub -I.. trade_test.cpp ../link.cpp -o /tmp/trade_test
/tmp/trade_test

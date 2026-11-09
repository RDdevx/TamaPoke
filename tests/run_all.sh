#!/bin/sh
# Every PC-side test in one go. Needs only g++ (and python3 is NOT needed).
set -e
cd "$(dirname "$0")"
I="-std=c++17 -O1 -Istub -I.."
echo "== trade protocol";       ./run_trade_test.sh | tail -1
echo "== battle order";         g++ $I battle_order_test.cpp ../battle.cpp ../pet.cpp ../party.cpp -o /tmp/t_battle && /tmp/t_battle | tail -1
echo "== revived pet";          g++ $I companion_test.cpp ../pet.cpp ../party.cpp -o /tmp/t_comp && /tmp/t_comp | tail -1
echo "== box migration/backup"; g++ $I box_migration_test.cpp ../party.cpp ../save.cpp -o /tmp/t_box && /tmp/t_box | tail -1

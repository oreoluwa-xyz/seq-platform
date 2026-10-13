#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
seq_test_root=$(git rev-parse --show-toplevel)
seq_test_output=$(mktemp -d /tmp/seq-engine.XXXXXX)
trap 'rm -rf "$seq_test_output"' EXIT
clang -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter \
    -Itests/seq_engine/support -I"$seq_test_root/quantum" -I. \
    tests/seq_engine/test_seq_engine.c seq_eeprom.c melodic_sequencer.c virtual_midi.c \
    sequencer_toggle.c drum_lane_pool.c seq_automation.c seq_module.c -o "$seq_test_output/engine"
"$seq_test_output/engine"
for capacity in 4 48 144; do
    clang -std=c11 -Wall -Wextra -Werror -I. -DSEQ_DRUM_LANE_POOL_CAPACITY="$capacity" \
        drum_lane_pool.c tests/seq_lane_pool/test_seq_lane_pool.c -o "$seq_test_output/pool"
    "$seq_test_output/pool"
done
clang -std=c11 -Wall -Wextra -Werror -I. sequencer_toggle.c tests/seq_toggle/test_seq_toggle.c -o "$seq_test_output/toggle"
"$seq_test_output/toggle"
PYTHONDONTWRITEBYTECODE=1 python3 tests/seq_engine/test_backup.py

node ui/test.mjs "$seq_test_output/engine"

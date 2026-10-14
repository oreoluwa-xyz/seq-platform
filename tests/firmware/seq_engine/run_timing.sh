#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
seq_test_root=$(git rev-parse --show-toplevel)
seq_test_output=$(mktemp -d /tmp/seq-timing.XXXXXX)
trap 'rm -rf "$seq_test_output"' EXIT
clang -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter \
    -Itests/seq_engine/support -I"$seq_test_root/quantum" -I. \
    tests/seq_engine/test_seq_timing.c seq_eeprom.c melodic_sequencer.c virtual_midi.c \
    sequencer_toggle.c drum_lane_pool.c seq_automation.c seq_module.c -o "$seq_test_output/timing"
"$seq_test_output/timing"
clang -std=c11 -Wall -Wextra -Werror -I. \
    tests/seq_engine/test_seq_midi_transport.c seq_midi_transport.c -o "$seq_test_output/transport"
"$seq_test_output/transport"

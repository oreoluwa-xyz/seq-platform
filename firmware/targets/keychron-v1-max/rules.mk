TAP_DANCE_ENABLE = no
ENCODER_MAP_ENABLE = no
MIDI_ENABLE = yes
MIDI_SHARED_EP = yes
KEYBOARD_SHARED_EP = yes
CONSOLE_ENABLE = no
COMMAND_ENABLE = no
MOUSEKEY_ENABLE = no
RAW_ENABLE = no
NKRO_ENABLE = no
OPT_DEFS += -UAPDAPTIVE_NKRO_ENABLE
OPT = s

SRC += seq_eeprom.c sequencer.c melodic_sequencer.c virtual_midi.c sequencer_toggle.c drum_lane_pool.c seq_automation.c seq_module.c

# The vendor post-task MIDI drain discards incoming packets. Route it through
# QMK's MIDI parser without changing the shared Keychron implementation.
LDFLAGS += -Wl,--wrap=midi_ep_task

# Keep project saves within the process-stack budget.
LDFLAGS += -Wl,--wrap=eeprom_update_block

# Generate once per make invocation; child makes inherit the same release ID.
ifndef SEQ_BUILD_ID
SEQ_BUILD_ID := $(shell python3 -c 'import uuid; print(uuid.uuid4().hex)')
endif
export SEQ_BUILD_ID
OPT_DEFS += -DSEQ_BUILD_ID=\"$(SEQ_BUILD_ID)\"

#pragma once

/* A fresh ID is supplied by rules.mk for each firmware build. */
#ifndef SEQ_BUILD_ID
#    define SEQ_BUILD_ID "00000000000000000000000000000000"
#endif
#define SEQ_FIRMWARE_VERSION "2026.10.14.1"
#define SEQ_FIRMWARE_TARGET "keychron/v1_max/ansi_encoder:seq_test"

typedef struct {
    char magic[8];
    char version[24];
    char target[48];
    char build_id[33];
} seq_firmware_identity_t;

static const seq_firmware_identity_t seq_firmware_identity = {
    .magic = "SEQFWID1",
    .version = SEQ_FIRMWARE_VERSION,
    .target = SEQ_FIRMWARE_TARGET,
    .build_id = SEQ_BUILD_ID,
};
_Static_assert(sizeof(seq_firmware_identity_t) == 113, "Firmware identity wire layout changed");
_Static_assert(sizeof(SEQ_BUILD_ID) == 33, "Build ID must contain 32 hex characters");

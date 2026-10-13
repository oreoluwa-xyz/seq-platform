Run from the `seq_test` keymap directory, using the same standalone host-test approach as `tests/seq_toggle`:

```sh
cc -std=c11 -Wall -Wextra -Werror -I. drum_lane_pool.c tests/seq_lane_pool/test_seq_lane_pool.c -o /tmp/seq_lane_pool_test
/tmp/seq_lane_pool_test
```

The tests cover distribution across track slots, independent step data, exhaustion, atomic group reservation, release/reuse, modifier ownership, trimming, resets, and invalid inputs. Also run with `-DSEQ_DRUM_LANE_POOL_CAPACITY=4` and `-DSEQ_DRUM_LANE_POOL_CAPACITY=144` to check the configurable pool bounds.

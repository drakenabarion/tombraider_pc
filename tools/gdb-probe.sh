#!/usr/bin/env bash
# Runs bb-probe under gdb and prints a backtrace where a bad_alloc is thrown (the GPU library
# stops on boost::container::bad_alloc; change the catch pattern for other exceptions), for run.py:
#   BB_PROBE=tools/gdb-probe.sh BB_GAME_DIR=... python3 run.py
# The runtime's own signals (guest faults, thread suspension) pass straight through.
here=$(cd -- "$(dirname -- "$0")/.." && pwd)
exec gdb -q -batch \
    -ex 'handle all nostop noprint pass' \
    -ex 'set print thread-events off' \
    -ex 'catch throw .*bad_alloc' \
    -ex run \
    -ex 'echo \n=== exception thrown here ===\n' \
    -ex 'bt 40' \
    -ex 'frame 11' -ex 'echo \n=== resource list sizes (buffers 40, images 64, samplers 16, fmasks 32) ===\n' \
    -ex 'print info.buffers.m_holder.m_size' -ex 'print info.images.m_holder.m_size' \
    -ex 'print info.samplers.m_holder.m_size' -ex 'print info.fmasks.m_holder.m_size' \
    -ex 'print/x info.pgm_hash' \
    --args "$here/out/bb-probe" "$@"

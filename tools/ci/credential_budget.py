# SPDX-License-Identifier: GPL-2.0-or-later
"""One cooperative deadline and explicit finite work budgets."""
import time
from scanner_runtime import ScannerError

ENTRIES = 10000
UNIQUE_BYTES = 128 * 1024 * 1024
PHYSICAL_BYTES = UNIQUE_BYTES
SINGLE_BYTES = 16 * 1024 * 1024
METADATA_BYTES = 8 * 1024 * 1024
FINDINGS = 16384
CHUNK = 65536


def tick(deadline):
    if time.monotonic() >= deadline:
        raise ScannerError('scanner-deadline')


def chunks(data, deadline):
    for start in range(0, len(data), CHUNK):
        tick(deadline)
        yield memoryview(data)[start:start + CHUNK]
    tick(deadline)

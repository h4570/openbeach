#pragma once

#include "core/char16.h"

namespace obl::platform {

// One game per game directory: copies in other directories (parallel test runs in separate checkouts,
// docs/testing/test-runner.md) can run at the same time. The lock is a file in the temp directory named after a
// hash of the game directory, held open (Windows: without sharing, POSIX: flock) until the process ends.
void SetInstanceLockDirectory(const Text16& gameDir);

// False when another game of the same directory holds the lock (the windowed game then quits silently).
bool AcquireInstanceLock();
void ReleaseInstanceLock();

} // namespace obl::platform

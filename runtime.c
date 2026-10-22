#include "backend.c"

/* Optional asynchronous timestamp stream implemented in timing.inc. */

/* GPU-generated indirect compute dispatch and persistent frame metadata. */

/* Native command packets, parallel recording, FIFO VkQueue ownership. */

/* Completion guards include deferred CPU recording and recycled query pools. */

/* Parallel frame completion and rejected-present retirement, revision 2. */

/* Shared GI uploads use owned queue-ordered update packets; page staging is per-slot. */

/* Command packet IDs are captured at begin and retained through submission. */

/* Async timing pairs wait for CPU packet completion; overlapping thread work is not summed. */

/* Cached UI surface packets and coherent GPU-local buffer placement with host fallback. */

/* Timestamp samples detach completed target fences before target retirement. */

/* Owner-thread WSI completes before the UI thread can commit the Wayland surface. */

/* Bulk completed-buffer copies and compute/transfer-to-host visibility. */

/* Owned devices enable supported independent auxiliary attachment writes. */

/* Optional temporal SDK ABI 2: compute visibility and FIFO SDK recording lane. */

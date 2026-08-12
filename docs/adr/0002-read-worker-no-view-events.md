# Live reads use cooperative UART waits and worker-thread stop flags

A live read worker writes discovered tags into the shared `UHFTagWrapper`; the read view's 300 ms timer snapshots that wrapper on the GUI thread for the live EPC/RSSI display. The worker does not stream view-dispatcher events during scanning. Stop is signalled with `UHF_WORKER_FLAG_STOP`, a Furi thread flag, and every worker loop—including the in-flight single-poll UART receive wait—observes the same flag. The receive waits block for one scheduler tick periodically so lower-priority GUI/timer services can run; `furi_thread_yield()` is insufficient because it only schedules equal-priority threads. Once the flag is visible, `uhf_worker_stop()` may synchronously join the worker.

## Context

Single-poll used `setup_and_send_rx()` in a tight loop. Its receive wait was a 70,000-iteration busy decrement that could be extended by each UART byte, never yielded, and could not observe `UHFWorkerStateStop` until the whole poll returned. This caused two coupled failures: the GUI's periodic live update could be starved, and pressing Stop block-joined a worker trapped inside that non-cancellable wait. The former made the screen static; the latter froze the app and left the Flipper Loader locked.

`UHFWorker.state` remains the operation selector, but it is not the cross-thread cancellation primitive. Furi thread flags provide the synchronized stop signal and are visible inside the innermost receive loop.

## Consequences

- Every cancellable worker loop uses `uhf_worker_stop_requested()`; do not poll `UHFWorker.state` for cross-thread cancellation.
- Any UART receive loop used by a live worker must check the stop flag and periodically block for one scheduler tick while waiting; a same-priority yield is insufficient.
- Do not reintroduce per-frame worker-to-GUI `view_dispatcher_send_custom_event` calls. Live display updates are pulled by the read view's 300 ms timer.
- The live counter/RSSI refreshes at the timer cadence rather than per frame.
- The GUI reads fixed-lifetime wrapper slots while scanning. A concurrent in-place update may produce one cosmetic torn frame, but the slot is never freed until the worker has stopped.

# Split single-tag and simultaneous reads into two explicit modes

The Read screen offers two menu entries — **Read (Multi)** (default) and **Read (Single)** — both driving the same `UHFReaderViewRead`, differing only in the worker's inner loop. Multi runs the passive `m100_multi_poll` inventory stream to collect many EPCs; Single runs the aggressive single-poll retry loop for range, locks its published slot to the first successfully returned EPC in software, and updates only that EPC's live RSSI. Single deliberately does **not** auto-read TID/User/Reserved (those are deep-read on demand via Right, the same path Multi uses). No hardware Select filter is installed, so a Single scan cannot constrain a later Multi scan.

## Considered Options

- **Unified multi-poll for everything** (the state after `dadc02f`): simplest UX, but the passive stream has materially shorter read range than the retry loop, which is why single-tag range regressed. Rejected.
- **Restore old single-read verbatim** (EPC poll + immediate bank reads): the EPC reads at range but the follow-on bank reads usually fail at that same range, surfacing spurious "read failed". Rejected in favor of EPC-only + on-demand deep read.

## Consequences

Do not re-merge the two modes back into one multi-poll to "simplify" — that reintroduces the range regression. Single mode intentionally keeps exactly one fixed tag in the wrapper: the first successfully returned EPC owns the scan, later non-matching EPCs are ignored, and matching polls refresh RSSI only. This lock is software-only; it must not change persistent module Select state.

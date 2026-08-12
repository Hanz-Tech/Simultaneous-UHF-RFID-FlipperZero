# Split single-tag and simultaneous reads into two explicit modes

The Read screen offers two menu entries — **Read (Multi)** (default) and **Read (Single)** — both driving the same `UHFReaderViewRead`, differing only in the worker's inner loop. Multi runs the passive `m100_multi_poll` inventory stream to collect many EPCs; Single runs the aggressive `send_polling_command` retry loop on one tag with live RSSI, and deliberately does **not** auto-read TID/User/Reserved (those are deep-read on demand via Right, the same path Multi uses).

## Considered Options

- **Unified multi-poll for everything** (the state after `dadc02f`): simplest UX, but the passive stream has materially shorter read range than the retry loop, which is why single-tag range regressed. Rejected.
- **Restore old single-read verbatim** (EPC poll + immediate bank reads): the EPC reads at range but the follow-on bank reads usually fail at that same range, surfacing spurious "read failed". Rejected in favor of EPC-only + on-demand deep read.

## Consequences

Do not re-merge the two modes back into one multi-poll to "simplify" — that reintroduces the range regression. Single mode intentionally keeps exactly one tag in the wrapper; it is not a slower Multi.

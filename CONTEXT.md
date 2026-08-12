# Simultaneous UHF RFID Reader

A Flipper Zero app that drives a YRM100X (M100/QM100) UHF module to read, write, clone, lock, and kill EPC Gen2 tags — including reading many tags at once.

## Language

**EPC**:
The identifier a UHF tag backscatters when polled; the primary thing the app collects and displays.
_Avoid_: UID, tag ID (that's the TID)

**Single Poll**:
Reading one tag by firing the poll command in a tight retry loop for maximum range. The first returned EPC remains the scan target; later responses are software-filtered so only that EPC can refresh its live RSSI.
_Avoid_: single read, detect single

**Multi Poll** (a.k.a. **Simultaneous Read**):
Collecting many distinct tags' EPCs from one continuous inventory session, deduplicating as they stream in. Favors breadth over range.
_Avoid_: multi read, detect multiple, inventory scan

**Deep Read**:
Fetching a selected tag's non-EPC banks (TID, User, Reserved) after it has been picked from a Multi Poll list.
_Avoid_: bank read, full read

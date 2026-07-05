Here's a clean Markdown version of the **Lock Tag Data Storage Area (Command 0x82)** protocol section with properly formatted tables.

---

# Lock Tag Data Storage Area (0x82)

2.10.1 Command Frame
For a single tag, lock or unlock the tag's memory area. This command should be
preceded by setting the Select parameter to choose the specific tag for the Lock operation.
For example, to lock the Access Password, use the following command:


## Command Frame

Lock or unlock a tag memory area for a selected tag.

### Parameters

| Field             | Value        |
| ----------------- | ------------ |
| Type              | `0x00`       |
| Command           | `0x82`       |
| Parameter Length  | `0x0007`     |
| Access Password   | `0x0000FFFF` |
| Lock Operation-LD | `0x020080`   |
| Checksum          | `0x09`       |

### Command Frame Structure

| Header | Type | Command | PL (MSB) | PL (LSB) | AP Byte 3 | AP Byte 2 | AP Byte 1 |
| ------ | ---- | ------- | -------- | -------- | --------- | --------- | --------- |
| BB     | 00   | 82      | 00       | 07       | 00        | 00        | FF        |

| AP Byte 0 | LD Byte 2 | LD Byte 1 | LD Byte 0 | Checksum | End |
| --------- | --------- | --------- | --------- | -------- | --- |
| FF        | 02        | 00        | 80        | 09       | 7E  |

### Complete Frame

```text
BB 00 82 00 07 00 00 FF FF 02 00 80 09 7E
```

---

# Lock Command Payload Format

The upper 4 bits of the Lock Data (LD) field are reserved.

The remaining 20 bits contain:

* 10 Mask bits
* 10 Action bits

Ordered from MSB to LSB.

## Bit Layout

| Bit | Meaning       |
| --- | ------------- |
| 19  | Kill Mask     |
| 18  | Kill Mask     |
| 17  | Access Mask   |
| 16  | Access Mask   |
| 15  | EPC Mask      |
| 14  | EPC Mask      |
| 13  | TID Mask      |
| 12  | TID Mask      |
| 11  | User Mask     |
| 10  | User Mask     |
| 9   | Kill Action   |
| 8   | Kill Action   |
| 7   | Access Action |
| 6   | Access Action |
| 5   | EPC Action    |
| 4   | EPC Action    |
| 3   | TID Action    |
| 2   | TID Action    |
| 1   | User Action   |
| 0   | User Action   |

---

## Mask Field Definitions

A mask bit pair determines whether the associated Action field is applied.

| Memory Area     | Mask Bits |
| --------------- | --------- |
| Kill Password   | 19–18     |
| Access Password | 17–16     |
| EPC Memory      | 15–14     |
| TID Memory      | 13–12     |
| User Memory     | 11–10     |

### Mask Values

| Value | Meaning                      |
| ----- | ---------------------------- |
| `00`  | Ignore action for this field |
| `01`  | Reserved                     |
| `10`  | Apply action                 |
| `11`  | Reserved                     |

---

## Memory Bank Action Values

| pwd-write | perma-lock | Description                                              |
| --------- | ---------- | -------------------------------------------------------- |
| 0         | 0          | Memory bank writable from either open or secured state   |
| 0         | 1          | Memory bank permanently writable and may never be locked |
| 1         | 0          | Memory bank writable only from secured state             |
| 1         | 1          | Memory bank not writable from any state                  |

---

## Password Area Action Values

| pwd-read/write | perma-lock | Description                                                        |
| -------------- | ---------- | ------------------------------------------------------------------ |
| 0              | 0          | Password readable and writable from open or secured state          |
| 0              | 1          | Password permanently readable and writable and may never be locked |
| 1              | 0          | Password readable and writable only from secured state             |
| 1              | 1          | Password not readable or writable from any state                   |

---

# Successful Notification Frame

Returned when the lock operation succeeds.

### Parameters

| Field            | Value                         |
| ---------------- | ----------------------------- |
| Type             | `0x01`                        |
| Command          | `0x82`                        |
| Parameter Length | `0x0010`                      |
| UL               | `0x0E`                        |
| PC               | `0x3400`                      |
| EPC              | `0x30751FEB705C5904E3D50D70`  |
| Data             | `0x00` (Execution Successful) |
| Checksum         | `0xE2`                        |

### Frame Structure

| Header | Type | Command | PL (MSB) | PL (LSB) | UL | PC (MSB) | PC (LSB) |
| ------ | ---- | ------- | -------- | -------- | -- | -------- | -------- |
| BB     | 01   | 82      | 00       | 10       | 0E | 34       | 00       |

### EPC

| EPC Byte 13 | EPC Byte 12 | EPC Byte 11 | EPC Byte 10 | EPC Byte 9 | EPC Byte 8 | EPC Byte 7 | EPC Byte 6 | EPC Byte 5 | EPC Byte 4 | EPC Byte 3 | EPC Byte 2 | EPC Byte 1 | EPC Byte 0 |
| ----------- | ----------- | ----------- | ----------- | ---------- | ---------- | ---------- | ---------- | ---------- | ---------- | ---------- | ---------- | ---------- | ---------- |
| 30          | 75          | 1F          | EB          | 70         | 5C         | 59         | 04         | E3         | D5         | 0D         | 70         | —          | —          |

### Tail

| Parameter | Checksum | End |
| --------- | -------- | --- |
| 00        | E2       | 7E  |

---

# Error Response: Tag Not Found / EPC Mismatch

Returned when the selected tag cannot be found.

### Parameters

| Field            | Value    |
| ---------------- | -------- |
| Type             | `0x01`   |
| Command          | `0xFF`   |
| Parameter Length | `0x0001` |
| Parameter        | `0x13`   |
| Checksum         | `0x14`   |

### Frame

| Header | Type | Command | PL (MSB) | PL (LSB) | Parameter | Checksum | End |
| ------ | ---- | ------- | -------- | -------- | --------- | -------- | --- |
| BB     | 01   | FF      | 00       | 01       | 13        | 14       | 7E  |

---

# Error Response: Incorrect Access Password

Returned when the supplied access password is invalid.

### Parameters

| Field            | Value                        |
| ---------------- | ---------------------------- |
| Type             | `0x01`                       |
| Command          | `0xFF`                       |
| Parameter Length | `0x0010`                     |
| Parameter        | `0x16`                       |
| UL               | `0x0E`                       |
| PC               | `0x3400`                     |
| EPC              | `0x30751FEB705C5904E3D50D70` |
| Checksum         | `0x75`                       |

### Frame Structure

| Header | Type | Command | PL (MSB) | PL (LSB) | Parameter | UL | PC (MSB) | PC (LSB) |
| ------ | ---- | ------- | -------- | -------- | --------- | -- | -------- | -------- |
| BB     | 01   | FF      | 00       | 10       | 16        | 0E | 34       | 00       |

### EPC

| EPC Byte 13 | EPC Byte 12 | EPC Byte 11 | EPC Byte 10 | EPC Byte 9 | EPC Byte 8 | EPC Byte 7 | EPC Byte 6 | EPC Byte 5 | EPC Byte 4 | EPC Byte 3 | EPC Byte 2 | EPC Byte 1 |
| ----------- | ----------- | ----------- | ----------- | ---------- | ---------- | ---------- | ---------- | ---------- | ---------- | ---------- | ---------- | ---------- |
| 30          | 75          | 1F          | EB          | 70         | 5C         | 59         | 04         | E3         | D5         | 0D         | 70         | 70         |

### Tail

| Checksum | End |
| -------- | --- |
| 75       | 7E  |

---

# EPC Gen2 Error Response

If the tag returns an EPC Gen2 protocol error, the reader returns:

```text
Returned Error = EPC Gen2 Error OR 0xC0
```

Example:

| EPC Gen2 Error        | Returned Error |
| --------------------- | -------------- |
| 0x04 (Memory Overrun) | 0xC4           |
| 0x03                  | 0xC3           |
| 0x02                  | 0xC2           |
| 0x01                  | 0xC1           |

### Example: Memory Overrun

| Field            | Value                        |
| ---------------- | ---------------------------- |
| Type             | `0x01`                       |
| Command          | `0xFF`                       |
| Parameter Length | `0x0010`                     |
| Parameter        | `0xC4`                       |
| UL               | `0x0E`                       |
| PC               | `0x3400`                     |
| EPC              | `0x30751FEB705C5904E3D50D70` |
| Checksum         | `0x23`                       |

This format preserves the tables correctly for Markdown parsers, GitHub rendering, and LLM document ingestion.

# Russound Controllers RNET™ Protocol & Specifications: RS-232 Communication

Markdown transcription of Russound's *RNET™ Protocol & Specifications, RS-232 Communication*, document
version 1.00.4 (source: [`russound-rnet-protocol.pdf`](russound-rnet-protocol.pdf)). Covers the ACA, CAV,
CAM, CAA and CAS controllers.

> **About this transcription (RNET Bridge project).**
> Section numbers, byte numbers and wording follow the PDF so the two can be cross-referenced. Spelling
> slips ("Bess", "Even Timestamp") were fixed silently. **Technical problems are never fixed silently.**
> Each one is kept as printed and flagged in an **Erratum** callout like this one, with the evidence.
> All errata are collected in [Appendix A](#appendix-a-errata-index).
>
> Every frame with a printed checksum was recomputed. That covers all 122 rows of the §13 hex table and
> every example in the text, and found one bad checksum in the examples (§6.2).
>
> Hardware-verified behaviour goes in [`../rnet-notes.md`](../rnet-notes.md). Where this document and
> real hardware disagree, hardware wins.
>
> Notation: in frame templates, lowercase placeholders (`cc`, `zz`, `kk`, `xx`) and `##` are variable bytes
> (shown in **bold** in the PDF). Every other byte must be sent exactly as shown.

## Quick Reference Guide

● = fully supported, ○ = partially supported, blank = not supported.

| Feature | CAS44 | CAA66 | CAM6.6 | CAV6.6 | ACA-E5\* | Pg. |
|---|:-:|:-:|:-:|:-:|:-:|--:|
| Keypad Events | ● | ● | ● | ● | ● | 13 |
| Source Control Events | | ● | ● | ● | ● | 15 |
| Set Zone On/Off | ● | ● | ● | ● | ● | 19 |
| Set All Zones On | | | ● | ● | ● | 20 |
| Set All Zones Off | ● | ● | ● | ● | ● | 20 |
| Zone Source | ● | ● | ● | ● | ● | 20 |
| Zone Volume | ● | ● | ● | ● | ● | 22 |
| Bass | ● | ● | ● | ● | ● | 23 |
| Treble | ● | ● | ● | ● | ● | 25 |
| Loudness | ● | ● | ● | ● | ● | 27 |
| Balance | ● | ● | ● | ● | ● | 29 |
| Turn On Volume | ● | ● | ● | ● | ● | 31 |
| Background Color | | | ● | ● | | 33 |
| Do Not Disturb | | | ● | ● | ● | 35 |
| Party Mode | | | ● | ● | ● | 37 |
| All Zone Info | ○ | ○ | ● | ● | ● | 39 |
| Displaying a String | ● | ● | ● | ● | ● | 40 |
| Display Messages | ● | ● | ● | ● | ● | 42 |

\* For third-party integration with the ACA-E5, see chapter 2.

> **Note (project).** According to this table, the **CAA66 does not support Set All Zones On** (All Zones
> Off is supported). All Zone Info is only *partially* supported on the CAA66. §8.12 says the Party Mode
> and DND fields are unavailable there. Verify All On on hardware.

> **Erratum (minor).** "Displaying a String" is listed on page 40. It starts on page 41.

## 1 Introduction

This document describes how to control and interpret data from Russound Controllers. Remote access is
provided through the RS-232 serial port located on the rear panel of the Controllers (CAV also has front
panel access) using the following RNET™ Communications Protocol. It is assumed that the reader is familiar
with the features and operation of the Controller being used. All commands use Hexadecimal or HEX values.
In descriptive text these are indicated by preceding the value with a **0x**. Zero-based values are also
used in certain areas and are noted as such. This means that a value of **0x00 = 1, 0x01 = 2, and 0x02 = 3**
etc… Throughout this document all bytes within message packets *not* in bold must be referenced exactly as
they appear.

## 2 Overview

Russound Controllers use a comprehensive Communications Protocol called RNET™ which has been extended to the
RS-232 port. Through this port, virtually all aspects of the device operation can be performed. This
document provides detailed descriptions of messages required to perform basic device operation, as well as
instructions on how to interpret Display Feedback messages.

When processing incoming 3rd Party requests, the ACA-E5 uses the 'Source Device ID' (see section 4.1.3 for
more details on the Source Device ID) to determine which controller and zone is being accessed. Throughout
the document, the Source Device ID field has been updated to reflect this (new) requirement.

In order to gain access to zone-specific data using the Set and Get messages (specified throughout
section 7), it is necessary to issue at least one 'Event' message (see section 4.2.2 for more details on
Event messages). This may be any of the existing Event messages or the new 'Device Restart' Event message
(see section 5).

> **Erratum (minor).** The Set and Get messages are specified in section **8**, not section 7.

## 3 COM Port

The following information is needed to allow connection and communication with RNET™ Controllers.

### 3.1 COM Settings

The following settings must be made to the RS-232 serial COM port used to interface with the Controllers:

- 19200 baud rate
- 1 Start bit
- 1 Stop bit
- No flow control
- No parity

> **Erratum (omission).** The number of data bits is not stated. It is 8, since the protocol carries
> full 8-bit bytes (0xF0, 0xF1, 0xF7). The port is 19200 8N1.

### 3.2 Connector Type / Pin-out

**Connector:** Female DB-9

| Pin | Function |
|---|---|
| 1 | NC |
| 2 | Transmit |
| 3 | Receive |
| 4 | NC |
| 5 | Ground |
| 6 | NC |
| 7 | NC |
| 8 | NC |
| 9 | NC |

## 4 RNET™ Message Packet Format

```
|                     Header                       |  Body  |     Trailer      |
| 0xF0 | Target Device ID | Source Device ID | Type |  Body  | Checksum | 0xF7 |
```

Every RNET™ message has a consistent format. There are three major components: Message Header, Message
Body and Message Trailer. Each of these is explained here in some detail.

MSB stands for Most Significant Bit which is the far left Bit of an 8-Bit Byte (MSB = Bit 7 =
**0**0010001). There are several special characters used in this protocol. These special characters have
the MSB set to high (1). All Bytes within the message of an RNET™ packet have the MSB set to low (0) except
for three special characters. The special characters with the MSB set to high (1) are the "Start of Message
Character", the "End of Message Character", and the "Invert Character", which are explained within this
document.

### 4.1 Message Header

Each message consists of a "Message Header" which is compiled of a Start of Message Character, Target
Device ID, Source Device ID, and the Message Type.

#### 4.1.1 Start of Message Character

As previously stated, there are several special characters. One of these is the Start of Message Character
0xF0 which is a special HEX value that indicates the beginning of a new message.

#### 4.1.2 Target Device ID

The Target Device ID defines to which device the message is being sent. Every device on an RNET™ system has
a unique "Device ID" that allows messages to be sent to it. Each Device ID consists of a Controller ID, a
Zone (Port) ID, and a Keypad ID. In the case of a Controller with zone Keypads, the purpose of each Device
ID field is apparent.

##### 4.1.2.1 Target Controller ID

On power up, the Controller assigns its Controller ID as well as a Zone ID and Keypad ID to all Keypads
connected to it. The Controller ID is set in the Controllers "Setup Menu" using standard numbering (1 = 1,
2 = 2, etc...) but in the Device ID it is Zero-based (0x00 = 1, 0x01 = 2, 0x02 = 3, etc...). All systems
have a controller 0x00 (the Root Controller) with additional controllers added (and numbered) as needed. A
value of 0x7D can be used to send messages to all Devices in the system.

The Russound RNET™ system provides support for up to six connected Controllers for a maximum of 36 separate
Zones. When a multiple Controller system is configured, each Controller is given a unique Controller ID
through the programming procedure. All of the Controllers are linked together onto a common RNET™ bus, so
any messages sent to the serial port of one Controller will be available to all Controllers and all Keypads
in the system.

All of the messages described in this document can be sent to any of the Controllers in the system by
simply changing the Target Controller ID to match the Controller you would like to send the message to.
There are several special Controller ID values that have been defined:

| Value (Hex) | Description |
|---|---|
| 7F | All Keypads |
| 7E | All Controllers |
| 7D | All Devices |

##### 4.1.2.2 Target Zone (Port) ID

Zone ID defines the Zone for a particular device. For keypads, this defines which zone port on the
Controllers rear panel that the keypad is connected to. The following special Zone ID values have been
defined:

| Value (Hex) | Description |
|---|---|
| 7F | Reserved |
| 7E | Controller Link |
| 7D | Peripheral Device (Internal Tuner, ST2-XM, etc...) |
| 7C | Trace |

Aside from the above list of special values and those for the actual ports on the back of a Controller
(4-Zone Controllers = 0x00 - 0x03) (6-Zone Controllers = 0x00 - 0x05), any value can be used for the Zone
ID field.

##### 4.1.2.3 Target Keypad ID

Keypad ID identifies particular Keypads sharing a single Zone on the Controller or the Source Number of an
Internal Tuner (AM/FM or XM for CAM only) when the Zone ID is set as Peripheral. NOTE: The CAM6.6 will only
allow the Internal Tuner to be set as Source 1 and therefore must use a value of 0x00 in this field. The
following values have specific meaning in the system:

| Value (Hex) | Description |
|---|---|
| 7F | The Controller Itself |
| 7E | Reserved |
| 7D | Targets all Keypads on a particular zone or all Sources for connected Peripheral Devices |
| 7C | Used by Keypads to request an ID from the Controller |
| 79 | Indicates a Source Broadcast Display Feedback message |

Aside from the above list of special values, all other Keypad IDs have not been formally assigned so they
can be used as needed, any value can be used for the Zone ID field.

##### 4.1.2.4 Target Device ID Example

Example 1 shows a complete Target Device ID for a Controller that has the Controller ID set to 1 and a
Keypad plugged into Zone 1.

| Value (Hex) | Device ID Field | Description |
|---|---|---|
| 00 | Controller ID | Controller 1 = 0x00 (Zero-based, see 4.1.2.1) |
| 00 | Zone (Port) ID | Zone 1 = 0x00 (Zero-based, see 4.1.2.2) |
| 7F | Keypad ID | The Controller Itself |

Example 2 shows a complete Target Device ID for a message sent to Zone 1 of all Controllers in the system
such as All On / All Off.

| Value (Hex) | Device ID Field | Description |
|---|---|---|
| 7E | Controller ID | All Controllers |
| 00 | Zone (Port) ID | Zone 1 = 0x00 (Zero-based, see 4.1.2.2) |
| 7F | Keypad ID | The Controller Itself |

#### 4.1.3 Source Device ID

The "Source" Device ID is the identification of the device that is sending the message. For external
devices controlling the system, like an automation and control system, these can be any value that is a
unique value among devices attached to the system. The recommend Device ID for external control systems is
Controller ID: 0x00, Zone ID: 0x00, and Keypad ID: 0x70 (0x71 when connected to an ACA-E5).

##### 4.1.3.1 Source Controller ID

For 3rd party devices, this should be set to a value of '0x00'.

##### 4.1.3.2 Source Zone (Port) ID

For 3rd party devices, this should be set to a unique value among devices attached to the system.

##### 4.1.3.3 Source Keypad ID

The recommend Keypad ID for external control systems is 0x70 (0x71 when connected to an ACA-E5), which is a
Keypad ID other than those used in the system.

> **Note (project): the Source Zone ID is used inconsistently.** §4.1.3 recommends Source Zone ID `00`.
> But every frame template in §5–§8 puts `zz` (the addressed zone) there, and the §6.1/§6.2 breakdowns
> label it "Source Zone ID: **Zone affected**". The §13 hex table uses `zz` for keypad events and source
> select, but `00` for Zone On/Off. Zone On/Off carries the zone in its event data, so both forms are
> plausible there. Keypad events and Source Select carry no zone anywhere else, so they must put the
> zone in byte 6. See the note under §8.1.1.

#### 4.1.4 Message Type

This value defines the type of message that is being sent. The most important Message Types are as follows:

| Value (Hex) | Message Type | Description |
|---|---|---|
| 00 | Set Data | Sets a parameter's value |
| 01 | Request Data | Requests a parameter's value |
| 02 | Handshake | Acknowledges a data send |
| 05 | Event | Triggers a system response that may set a parameter value, update displays, etc... |

In some cases, setting parameter values can be done in two ways. A Set Data message can be sent directly to
the parameter, or an Event message can be sent to trigger the Controller to set the value instead. Event
messages are a little easier to use, and may trigger other desired results (such as updating Keypad
displays, updating related parameters, etc...) where a Set Data message may not. Because of this, this
document describes using Event messages to set parameter values where it is most desirable.

##### 4.1.4.1 Paths

The Data within each RNET device is organized in a hierarchical tree similar to a computer's disk drive.
When sending Data or Events from one system to another, a Path is specified to define exactly which object
should be modified. Target and Source Paths are usually defined, but for the sake of remotely controlling
the system, only the Target Path is of interest.

Paths in RNET messages are defined by first specifying how deep the path is, followed by the numeric values
of each directory. For example, a Path that was three levels deep might be represented as "3.1.2.8". The
"3" at the beginning defines how deep the Path is. The numbers that follow are the subdirectories. Think of
the numbers as folder names (folder "8" is inside folder "2" which is inside folder "1"). Bear in mind that
the folders are unique for each level, so folder "1" can be in folder "1" of the next level up (just like
folders on a computer).

### 4.2 Message Body

The message body contains specific data which varies in value and byte count depending on the particular
Message Type being sent. Refer to the particular messages.

#### 4.2.1 The Invert Character

The Invert Character is used in special cases as part of the Message Body. If the data in an RNET™ message
body includes any byte values that have the MSB set to 1 (they have a hex value greater than 0x7F), the byte
will be rejected as only the lower 7 bits are used to hold data. In order to allow values greater than 0x7F
to be accepted, the byte must first be bitwise inverted (e.g., 10010101 = 01101010), and the special Invert
Character (0xF1) is inserted just prior to the inverted byte. When an RNET™ packet is received, the system
must detect the 0xF1 invert character. The 0xF1 character is then discarded and the following byte is
inverted back to its original value (e.g., 01101010 = 10010101).

Invert Character Usage Example. Sent message with inverted character:

| Value (Hex) | Notes |
|---|---|
| F0 | Start of message character |
| 00 | |
| 67 | |
| 7C | |
| F1 | Special Invert Character |
| 6A | Inverted Character (actual value 0x95) |
| 34 | Checksum |
| F7 | End of message character |

Received message after re-inverting character:

| Value (Hex) | Notes |
|---|---|
| F0 | Start of message character |
| 00 | |
| 67 | |
| 7C | |
| 95 | After re-inversion |
| XX | Checksum (Discarded) |
| F7 | End of message character |

#### 4.2.2 Event Messages

Event Messages trigger a system response that may set a parameter value, update displays, etc... Event
Messages are a little easier to use than Data Messages and may trigger other desired results (such as
updating Keypad displays, updating related parameters, etc...) where a Set Data message may not. Because of
this, this document describes using Event Messages to set parameter values where it is most desirable. An
Event Message Type consists of an Event ID, Event Timestamp, Event Data, and the Event Priority.

#### 4.2.3 Data Messages

A Set Data message can be sent directly to the parameter to set a parameter's value.

### 4.3 Message Trailer

The Message Trailer consists of the Checksum and End of Message Character. The Checksum value changes and
needs to be re-calculated whenever any one of the preceding characters in the message is changed. The
Checksum is always followed by the End of Message Character.

#### 4.3.1 Checksum

All messages include a Checksum that helps protect the integrity of the message. The Checksum is a single
byte that can be calculated using the following formula (see example below):

| Value (Hex) | Notes |
|---|---|
| F0 | Start of Message Character |
| 00 | |
| 67 | |
| 7C | |
| F1 | |
| 0F | |
| 59 | Checksum |
| F7 | End of Message Character |

**Step #1** - Add the HEX value of every byte in the message that precedes the Checksum:
Example - 0xF0 + 0x00 + 0x67 + 0x7C + 0xF1 + 0x0F = 0x02D3

**Step #2** - Count the number of bytes which precede the Checksum and convert that value from DEC to HEX
(byte count). Add the byte count in HEX to the previously calculated sum of bytes:
Example - 0x02D3 + 6 (6 = Decimal value byte count) = 0x02D9

**Step #3** - This value is then AND-ed with the HEX value 0x007F (7F is the highest BIN value for 7 bits =
1111111). The Checksum itself and the End of Message Character are not included in the calculation. Only
the low 7 bits are used so overflow is discarded:
Example - 0x02D9 AND 0x007F = 0x59 = Checksum

> **Note (project).** The sum and count are taken over the bytes *as transmitted*, after escaping. Any
> `F1` and the inverted byte that follows it are both included, as in this example. Because the checksum
> is masked to 7 bits, it never needs escaping.

#### 4.3.2 End of Message Character

The End of Message Character (0xF7) is the special HEX value that indicates the end of the message.

## 5 Device Restart Event

To indicate to the ACA-E5 controller that a Third Party device was added to the system, transmit a 'Device
Restart Event message' as presented here. As mentioned previously, any other Event message will serve to
indicate the addition of a Third Party device. In other words, a Device Restart Event is optional.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21
Value F0 00 00 7F cc zz kk 05 02 02 00 00 55 01 03 00 35 00 01 xx F7
```

- cc = controller number -1
- zz = zone number -1
- kk = 0x71
- xx = checksum

> **Note (project).** `kk = 0x71` is the ACA-E5 value. Other controllers use `0x70` (§4.1.3.3). With
> `cc = 00`, `zz = 00`, `kk = 70`, the checksum is `0A`.

## 6 Buttons and Keycodes

This section describes how to send various events associated with the Buttons found on the keypads and/or
the IR remote.

The method for transmitting these button events falls into two categories:

- **Keypad Events:** the events that the keypad is capable of sending.
- **Source Control Events:** all others. These are only available via the IR Remote.

Note: all values are in HEX.

### 6.1 Keypad Events

Here is a list of UNO Keypad Event values and a description of which buttons they apply to:

| Value (Hex) | Description |
|---|---|
| 64 | Setup Button |
| 67 | Previous |
| 68 | Next |
| 69 | Plus |
| 6A | Minus |
| 6B | Source (source toggle button) |
| 6C | Power |
| 6D | Stop |
| 6E | Pause |
| 6F | Favorite 1 |
| 70 | Favorite 2 |
| 73 | Play |
| 7F | Volume Up |
| 80 | Volume Down (because this value is > 7F, the special F1 character must be used) |

Here are some examples of some Keypad Events:

| Example | Frame |
|---|---|
| Zone 1, Volume Up | `F0 00 00 7F 00 00 70 05 02 02 00 00 7F 00 00 00 00 00 01 7B F7` |
| Zone 2, Play | `F0 00 00 7F 00 01 70 05 02 02 00 00 73 00 00 00 00 00 01 70 F7` |
| Zone 3, Favorite 1 | `F0 00 00 7F 00 02 70 05 02 02 00 00 6F 00 00 00 00 00 01 6D F7` |

Here is a break out of the Zone 1, Volume Up message (using above example of Keypad Event):

| Value | Field | Description |
|---|---|---|
| F0 | Start of Message | |
| 00 | Target Controller ID | Controller 1 |
| 00 | Target Zone ID | |
| 7F | Target Keypad ID | The Controller itself |
| 00 | Source Controller ID | |
| 00 | Source Zone ID | Zone affected |
| 70 | Source Keypad ID | Arbitrary Keypad ID (not otherwise used by the system) |
| 05 | Message Type | Event Message |
| 02 | Target Path, Num Levels | |
| 02 | Target Path, Level 1 | Root Menu |
| 00 | Target Path, Level 2 | Run Menu |
| 00 | Source Path, Num Levels | No Source Path is used |
| 7F | Event ID Lo Byte | Volume Up |
| 00 | Event ID Hi Byte | |
| 00 | Event Timestamp Lo Byte | unused |
| 00 | Event Timestamp Hi Byte | unused |
| 00 | Event Data Lo Byte | unused |
| 00 | Event Data Hi Byte | unused |
| 01 | Event Priority | Low Priority (does not generate a handshake) |
| 7B | Checksum | Recalculate when a preceding byte value is changed |
| F7 | End of Message | |

### 6.2 Source Control Events

Source Control Events are unique to the System Remote and are all sent using the special "Remote Control
Key" Event type with the actual Keycode passed in as the Data for the Event.

Here is a list of the Source Control Events (Keycodes):

| Value (Hex) | Description | Value (Hex) | Description | Value (Hex) | Description |
|---|---|---|---|---|---|
| 01 | "1" Button | 1A | Play | 33 | Surround Up |
| 02 | "2" Button | 1B | Stop | 34 | Surround Down |
| 03 | "3" Button | 1C | Search Forward | 35 | PIP |
| 04 | "4" Button | 1D | Search Rewind | 36 | PIP Move |
| 05 | "5" Button | 1E | Pause | 37 | PIP Swap |
| 06 | "6" Button | 1F | Record | 38 | Program |
| 07 | "7" Button | 20 | Menu | 39 | Sleep |
| 08 | "8" Button | 21 | Menu Up | 3A | On |
| 09 | "9" Button | 22 | Menu Down | 3B | Off |
| 0A | "0" Button | 23 | Menu Left | 3C | 11 |
| 0B | Volume Up | 24 | Menu Right | 3D | 12 |
| 0C | Volume Down | 25 | Select | 3E | 13 |
| 0D | Mute (for zone, not source) | 26 | Exit | 3F | 14 |
| 0E | Channel Up | 27 | Display | 40 | 15 |
| 0F | Channel Down | 28 | Guide | 41 | 16 |
| 10 | Power | 29 | Page Up | 42 | Bright |
| 11 | Enter | 2A | Page Down | 43 | Dim |
| 12 | Previous Channel | 2B | Disk | 44 | Close |
| 13 | TV/Video | 2C | Plus 10 | 45 | Open |
| 14 | TV/VCR | 2D | Open/Close | 46 | Stop 2 |
| 15 | A/B | 2E | Random | 47 | AM/FM |
| 16 | TV/DVD | 2F | Track Forward | 48 | Cue |
| 17 | TV/LD | 30 | Track Reverse | 49 | Disk Up |
| 18 | Input | 31 | Surround On/Off | 4A | Disk Down |
| 19 | TV/DSS | 32 | Surround Mode | 4B | Info |

Here are some examples of some Source Control Events:

| Example | Frame |
|---|---|
| Zone 1, Menu | `F0 00 00 7F 00 00 70 05 02 02 00 00 F1 40 00 00 00 20 00 01 4E F7` |
| Zone 2, Mute | `F0 00 00 7F 00 01 70 05 02 02 00 00 F1 40 00 00 00 0D 00 01 3C F7` |
| Zone 3, Record | `F0 00 00 7F 00 02 70 05 02 02 00 00 F1 40 00 00 00 1F 00 01 49 F7` |

> **Erratum (checksum).** The Zone 3, Record checksum is printed as `49`. The correct value is **`4F`**:
> sum + count = 0x5CF, and 0x5CF & 0x7F = 0x4F. A cross-check against Zone 1, Menu (`4E`) agrees: the
> zone byte goes up by 2 and the keycode goes down by 1 (0x20 → 0x1F), so 4E + 2 − 1 = 4F.

Here is a break out of the Zone 1, Menu message used in the above example:

| Value | Field | Description |
|---|---|---|
| F0 | Start of Message | |
| 00 | Target Controller ID | Controller 1 |
| 00 | Target Zone ID | |
| 7F | Target Keypad ID | The Controller itself |
| 00 | Source Controller ID | |
| 00 | Source Zone ID | Zone affected |
| 70 | Source Keypad ID | Arbitrary Keypad ID (not otherwise used by the system) |
| 05 | Message Type | Event Message |
| 02 | Target Path, Num Levels | |
| 02 | Target Path, Level 1 | Root Menu |
| 00 | Target Path, Level 2 | Run Menu |
| 00 | Source Path, Num Levels | No Source Path is used |
| F1 | Invert | Invert the next byte |
| 40 | Event ID Lo Byte | 40 = 0xBF inverted = Remote Control Key Release |
| 00 | Event ID Hi Byte | |
| 00 | Event Timestamp Lo Byte | unused |
| 00 | Event Timestamp Hi Byte | unused |
| 20 | Event Data Lo Byte | Menu (Keycode) |
| 00 | Event Data Hi Byte | unused |
| 01 | Event Priority | Low Priority (does not generate a handshake) |
| 7B | Checksum | Recalculate when a preceding byte value is changed |
| F7 | End of Message | |

> **Erratum (checksum).** The break-out lists the checksum as `7B`, which was copied from the §6.1
> break-out. The correct value is **`4E`**, as in the example frame above.

## 7 Using Request Messages

The Request Data message is used to receive parameter data from the Controller. This may be used to receive
Zone Power State, Volume Level, etc. When a Request Data message is sent to the Controller, a Set Data
message is generated by the Controller and sent back to the Request Message sender. Since the Set Data
message is of high priority, an Acknowledge message must be sent back to the Controller to acknowledge it
received the Request message. Failure to send the Acknowledge message will result in a system delay of
approximately 2.5 seconds. This is due to the Controller trying to re-send the Data.

Data can be requested discretely for each zone parameter, or all of the Zone information can be requested
in a single message.

> **Note (project).** The format of this Acknowledge message is never given. §11 shows only an *event*
> handshake. See SPEC §3.5 / §11.

## 8 Zones

This section will provide information in regards to messages that only apply to Zone commands. This will
include controlling the Zones as well as sending Request messages to "Get" the State of the Zones.
Zero-based values are used for the Zone Numbers (i.e. 0 = 1, 1 = 2, and 2 = 3 etc...)

Unless stated otherwise, the following legend applies to every template in this section:

- cc = controller number -1
- zz = zone number -1
- kk = keypad id = 0x70 (0x71 when connected to an ACA-E5)
- xx = checksum

> **Note (project): event body layout.** After the target path, every Event (`05`) body in this section
> is: Source Path count (`00`), Event ID lo/hi, Timestamp lo/hi, Data lo/hi, Priority. Event IDs above
> 0x7F are sent escaped: `F1 23` = 0xDC (Zone On/Off), `F1 22` = 0xDD (All On/Off), `F1 3E` = 0xC1
> (Source Select), `F1 21` = 0xDE (Set Volume), `F1 40` = 0xBF (Remote Control Key).

### 8.1 On/Off

The simplest way to explicitly turn a Zone On or turn a Zone Off is by using the discrete Zone On/Off Event.
The Event Data fields determine the Zone and On/Off state. This example shows using the Zone On/Off Event
message to execute a Zone On command for Zone 1 of Controller 1. Refer to the Buttons section for
information on toggling zone On/Off ("Power" command found under Keypad Events).

#### 8.1.1 Set State

Turn a specific Zone ON or OFF using a discrete message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22
Value F0 cc 00 7F cc zz kk 05 02 02 00 00 F1 23 00 ## 00 zz 00 01 xx F7
```

- kk = 0x70 (0x71 when connected to an ACA-E5).
- Byte #16 = 0x00 (off) or 0x01 (on)

> **Note (project).** This template puts `zz` in byte 6 (Source Zone ID). The §13 hex table sends every
> Zone On/Off with byte 6 = `00` and the zone only in byte 18. Both forms have valid checksums, for
> example Zone 2 On:
>
> - §13 table: `F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 01 00 01 00 01 13 F7`
> - this template: `F0 00 00 7F 00 01 70 05 02 02 00 00 F1 23 00 01 00 01 00 01 14 F7`
>
> Which form the CAA6.6 needs, or whether it accepts both, is to be verified on hardware (M2).

#### 8.1.2 Get State

This is the Request message for the On/Off State of the selected Zone.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
Value F0 cc 00 7F cc zz kk 01 04 02 00 zz 06 00 00 xx F7
```

The return message would look like the following.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
Value F0 cc zz kk cc 00 7F 00 00 04 02 00 zz 06 00 00 01 00 01 00 ## xx F7
```

- Byte #21 = 0x00 (off) or 0x01 (on)

#### 8.1.3 Set All Zones On/Off State

The RNET™ system can be sent a single message to issue an "All On" or "All Off" Event. The Controllers have
the ability to enable or disable the All On/All Off state control per zone. See the Product Manual for
instructions on programming each zone for "System On Enable" or "System On Disable".

Turn All Zones ON or OFF using a discrete message (all zones enabled in programming).

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22
Value F0 7E 00 7F cc zz kk 05 02 02 00 00 F1 22 00 ## 00 00 00 01 xx F7
```

- Byte #16 = 0x00 (all off) or 0x01 (all on)

> **Note (project).** Per the Quick Reference Guide, **All On is not supported on the CAA66**. All Off is.

### 8.2 Source Select

The simplest way to explicitly select a Source per Zone is by using the discrete Source Select Event. The
Event Data fields determine the Source being selected. Refer to the Buttons section for information on
stepping through Sources ("Source Toggle" command found under Keypad Events). Zero-based values are used for
the Source Numbers (i.e. 0 = 1, 1 = 2, and 2 = 3 etc...)

#### 8.2.1 Set Source

Select the Source for a particular zone using a discrete message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22
Value F0 cc 00 7F cc zz kk 05 02 00 00 00 F1 3E 00 00 00 ## 00 01 xx F7
```

- Byte #18 = selected source number -1

> **Note (project).** Byte 10 is `00` here, unlike the `02` used by the other zone events. This message
> has no zone field in its body, so the zone comes only from byte 6 (Source Zone ID). The §13 table
> agrees, and also lists source `06` as "AUX".

#### 8.2.2 Get Source

This is the Request message for what Source is selected on a particular Zone.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
Value F0 cc 00 7F cc zz kk 01 04 02 00 zz 02 00 00 xx F7
```

The return message would look like the following.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
Value F0 cc zz kk cc 00 7F 00 00 04 02 00 zz 02 00 00 01 00 01 00 ## xx F7
```

- Byte #21 = source number -1

### 8.3 Volume Select

The simplest way to explicitly select a Volume level per Zone is by using the discrete Volume Select Event.
The Event Timestamp fields determine the Volume Level. The Event Data fields determine the Zone affected by
the Volume being selected. Refer to the Buttons section for information on stepping through Volume levels
("Volume Up" and "Volume Down" commands found under UNO Keypad Events). Volume levels displayed on UNO
keypads range in value from 0 - 100 in steps of 2 (i.e. 0, 2, 4, 6, etc...). These Volume levels are
represented in Decimal when using RS-232 messages ranging in value from 0 - 50 (i.e. 0 = 0, 1 = 2, ...
2 = 4). In the actual Data message the HEX value is used which would range in value from 0x00 - 0x32. This
means (0x00 = 0 = 0, 0x01 = 1 = 2, 0x02 = 2 = 4 ... 0x32 = 50 = 100)

#### 8.3.1 Set Volume

Select the Volume for a particular zone using a discrete message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22
Value F0 cc 00 7F cc zz kk 05 02 02 00 00 F1 21 00 ## 00 zz 00 01 xx F7
```

- Byte #16 = volume level (0x00 - 0x32, 0x00 = 0 Displayed ... 0x32 = 100 Displayed)

#### 8.3.2 Get Volume

This is the Request message for what Volume level is selected on a particular Zone.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
Value F0 cc 00 7F cc zz kk 01 04 02 00 zz 01 00 00 xx F7
```

The return message would look like the following.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
Value F0 cc zz kk cc 00 7F 00 00 04 02 00 zz 01 00 00 01 00 01 00 ## xx F7
```

- Byte #21 = Volume Level -1

> **Erratum (suspected).** "Volume Level -1" is probably a copy-paste from §8.2.2 ("source number -1").
> Everywhere else, volume is the raw 0x00–0x32 value with no offset (§8.3.1, §8.12 byte 23). Verify on
> hardware.

### 8.4 Bass

Bass levels displayed on keypads range in value from -10 through +10. These Bass levels are represented in
Decimal when using RS-232 messages ranging in value from 0 - 20. In the actual Data message the HEX value is
used which would range in value from 0x00 - 0x14. This means (0x00 = 0 = -10, 0x01 = 1 = -9, 0x02 = 2 = -8
... 0x14 = 20 = +10).

NOTE: The keypad displays will not automatically update for this change.

#### 8.4.1 Bass Up/Bass Down

The following example shows the Bass level being increased or decreased.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc 00 7F cc zz kk 05 05 02 00 zz 00 00 00 ## 00 00 00 01 00 01 xx F7
```

- Byte #16 = button event (Plus = 0x69 = Increase, Minus = 0x6A = Decrease)

#### 8.4.2 Set Bass

Select the Bass level for a particular zone using a discrete message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc 00 7F cc zz kk 00 05 02 00 zz 00 00 00 00 00 01 00 01 00 ## xx F7
```

- Byte #22 = Bass level (0x00 = -10 ... 0x0A = Flat ... 0x14 = +10)

#### 8.4.3 Get Bass

The current Bass setting for a particular zone can be obtained by using the following message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18
Value F0 cc 00 7F cc zz kk 01 05 02 00 zz 00 00 00 00 xx F7
```

The return message would look like the following.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc zz kk cc 00 7F 00 00 05 02 00 zz 00 00 00 00 01 00 01 00 ## xx F7
```

- Byte #22 = Bass level (0x00 = -10 ... 0x0A = Flat ... 0x14 = +10)

### 8.5 Treble

Treble levels displayed on keypads range in value from -10 through +10. These Treble levels are represented
in Decimal when using RS-232 messages ranging in value from 0 - 20. In the actual Data message the HEX value
is used which would range in value from 0x00 - 0x14. This means (0x00 = 0 = -10, 0x01 = 1 = -9, 0x02 = 2 =
-8 ... 0x14 = 20 = +10).

NOTE: The keypad displays will not automatically update for this change.

#### 8.5.1 Treble Up/Treble Down

The following example shows the Treble level being increased or decreased.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc 00 7F cc zz kk 05 05 02 00 zz 00 01 00 ## 00 00 00 02 00 01 xx F7
```

- Byte #16 = button event (Plus = 0x69 = Increase, Minus = 0x6A = Decrease)

#### 8.5.2 Set Treble

Select the Treble level for a particular zone using a discrete message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc 00 7F cc zz kk 00 05 02 00 zz 00 01 00 00 00 01 00 01 00 ## xx F7
```

- Byte #22 = Treble level (0x00 = -10 ... 0x0A = Flat ... 0x14 = +10)

#### 8.5.3 Get Treble

The current Treble setting for a particular zone can be obtained by using the following message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18
Value F0 cc 00 7F cc zz kk 01 05 02 00 zz 00 01 00 00 xx F7
```

The return message would look like the following.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc zz kk cc 00 7F 00 00 05 02 00 zz 00 01 00 00 01 00 01 00 ## xx F7
```

- Byte #22 = Treble level (0x00 = -10 ... 0x0A = Flat ... 0x14 = +10)

### 8.6 Loudness

Loudness is displayed on keypads as "On" or "Off". Loudness can be toggled On or Off with a Plus or Minus
command. There can also be a discrete On or Off command selected.

NOTE: The keypad displays will not automatically update for this change.

#### 8.6.1 Loudness Toggle On/Off

The following example shows how to toggle the Loudness "On" and "Off" for a particular Zone.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc 00 7F cc zz kk 05 05 02 00 zz 00 02 00 ## 00 00 00 03 00 01 xx F7
```

- Byte #16 = button event (PLUS = 0x69 = On/Off toggle) or (MINUS = 0x6A = On/Off toggle)

#### 8.6.2 Set Loudness

Turn Loudness On or Off for a particular zone using a discrete message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc 00 7F cc zz kk 00 05 02 00 zz 00 02 00 00 00 01 00 01 00 ## xx F7
```

- Byte #22 = Loudness setting (0x00 = OFF, 0x01 = ON)

#### 8.6.3 Get Loudness

The current Loudness setting for a particular zone can be obtained by using the following message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18
Value F0 cc 00 7F cc zz kk 01 05 02 00 zz 00 02 00 00 xx F7
```

The return message would look like the following.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc zz kk cc 00 7F 00 00 05 02 00 zz 00 02 00 00 01 00 01 00 ## xx F7
```

- Byte #22 = Loudness setting (0x00 = OFF, 0x01 = ON)

### 8.7 Balance

Balance levels displayed on keypads range in value from "Left 10" to "Center" to "Right 10". These Balance
levels are represented in Decimal when using RS-232 messages ranging in value from 0 - 20. In the actual
Data message the HEX value is used which would range in value from 0x00 - 0x14. This means (0x00 = 0 =
Left 10 ... 0x0A = 10 = Center ... 0x14 = 20 = Right 10).

NOTE: The keypad displays will not automatically update for this change.

#### 8.7.1 Balance Left or Balance Right

The following example shows the Balance level being drawn More Left or More Right.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc 00 7F cc zz kk 05 05 02 00 zz 00 03 00 ## 00 00 00 04 00 01 xx F7
```

- Byte #16 = button event (Plus = 0x69 = More Left, Minus = 0x6A = More Right)

#### 8.7.2 Set Balance

Select the Balance level for a particular zone using a discrete message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc 00 7F cc zz kk 00 05 02 00 zz 00 03 00 00 00 01 00 01 00 ## xx F7
```

- Byte #22 = Balance level (0x00 = More Left ... 0x0A = Center ... 0x14 = More Right)

#### 8.7.3 Get Balance

The current Balance setting for a particular zone can be obtained using the following message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18
Value F0 cc 00 7F cc zz kk 01 05 02 00 zz 00 03 00 00 xx F7
```

The return message would look like the following.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc zz kk cc 00 7F 00 00 05 02 00 zz 00 03 00 00 01 00 01 00 ## xx F7
```

- Byte #22 = Balance level (0x00 = More Left ... 0x0A = Center ... 0x14 = More Right)

### 8.8 Turn On Volume

Turn On Volume levels displayed on keypads range in value from 0 - 100 in steps of 2 (i.e. 0, 2, 4 ...
100). These Turn On Volume levels are represented in Decimal when using RS-232 messages ranging in value
from 0 - 50. In the actual Data message the HEX value is used which would range in value from 0x00 - 0x32.
This means (0x00 = 0 = 0, 0x01 = 1 = 2, 0x02 = 2 = 4 ... 0x32 = 50 = 100)

NOTE: The keypad displays will not automatically update for this change.

#### 8.8.1 Increase or Decrease Turn On Volume

The following example shows the Turn On Volume level being increased or decreased.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc 00 7F cc zz kk 05 05 02 00 zz 00 04 00 ## 00 00 00 05 00 01 xx F7
```

- Byte #16 = button event (Plus = 0x69 = More Left, Minus = 0x6A = More Right)

> **Erratum.** "More Left / More Right" was copied from §8.7.1. For Turn On Volume, Plus = 0x69 =
> Increase and Minus = 0x6A = Decrease.

#### 8.8.2 Set Turn On Volume

Select the Turn On Volume level for a particular zone using a discrete message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc 00 7F cc zz kk 00 05 02 00 zz 00 04 00 00 00 01 00 01 00 ## xx F7
```

- Byte #22 = Turn On Volume level (0x00 - 0x32, 0x00 = 0 ... 0x32 = 100)

#### 8.8.3 Get Turn On Volume

The current Turn On Volume for a particular zone can be obtained using the following message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18
Value F0 cc 00 7F cc zz kk 01 05 02 00 zz 00 04 00 00 xx F7
```

The return message would look like the following.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc zz kk cc 00 7F 00 00 04 02 00 zz 01 00 00 00 01 00 01 00 ## xx F7
```

- Byte #22 = Turn On Volume level (0x00 - 0x32, 0x00 = 0 ... 0x32 = 100)

> **Erratum (suspected).** The reply path `04 02 00 zz 01 00 00` was copied from Get Volume (§8.3.2).
> Following the pattern of every other
> Get Zone Param reply (§8.4.3–§8.7.3), the expected reply is
> `F0 cc zz kk cc 00 7F 00 00 05 02 00 zz 00 04 00 00 01 00 01 00 ## xx F7`. Verify on hardware.

### 8.9 Background Color

Background Color is displayed on keypads as "Amber", "Green", or "Off". The Background Color can be toggled
through these selections with a Plus or Minus command. There can also be a discrete command selected for
each choice.

NOTE: The keypad displays WILL automatically update for this selection.

#### 8.9.1 Background Color Off/Amber/Green toggle

The following example shows how to toggle the Background Color for a particular Zone.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 00 00 7F cc zz kk 05 05 02 00 zz 00 05 00 ## 00 00 00 06 00 01 xx F7
```

- Byte #16 = button event (Plus = 0x69 = Off/Color toggle) or (Minus = 0x6A = Off/Color toggle)

#### 8.9.2 Set Background Color

Select the Background Color for a particular zone using a discrete message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 00 00 7F cc zz kk 00 05 02 00 zz 00 05 00 00 00 01 00 01 00 ## xx F7
```

- Byte #22 = Background Color (0x00 = Off, 0x01 = Amber, 0x02 = Green)

#### 8.9.3 Get Background Color

The current Background Color for a particular zone can be obtained using the following message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18
Value F0 cc 00 7F cc zz kk 01 05 02 00 zz 00 05 00 00 xx F7
```

The return message would look like the following.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc zz kk cc 00 7F 00 00 05 02 00 zz 00 05 00 00 01 00 01 00 ## xx F7
```

- Byte #22 = Background Color (0x00 = Off, 0x01 = Amber, 0x02 = Green)

### 8.10 Do Not Disturb

Do Not Disturb is displayed on keypads as "On" or "Off". DND can be toggled On or Off with a Plus or Minus
command. There can also be a discrete On or Off command selected.

#### 8.10.1 Do Not Disturb On/Off Toggle

The following example shows how to toggle Do Not Disturb "On" and "Off" for a particular Zone.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 00 00 7F cc zz kk 05 05 02 00 zz 00 06 00 ## 00 00 00 07 00 01 xx F7
```

- Byte #16 = button event (Plus = 0x69 = On/Off toggle) or (Minus = 0x6A = On/Off toggle)

#### 8.10.2 Set Do Not Disturb

Turn DND On or Off for a particular zone using a discrete message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 00 00 7F cc zz kk 00 05 02 00 zz 00 06 00 00 00 01 00 01 00 ## xx F7
```

- Byte #22 = Do Not Disturb setting (0x00 = OFF, 0x01 = ON)

#### 8.10.3 Get Do Not Disturb

The current DND setting for a particular zone can be obtained by using the following message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18
Value F0 cc 00 7F cc zz kk 01 05 02 00 zz 00 06 00 00 xx F7
```

The return message would look like the following.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc zz kk cc 00 7F 00 00 05 02 00 zz 00 06 00 00 01 00 01 00 ## xx F7
```

- Byte #22 = Do Not Disturb setting (0x00 = OFF, 0x01 = ON)

### 8.11 Party Mode

Party Mode is displayed on keypads as "On", "Off", or "Master". Party Mode can be toggled to "On" or
"Master" with the Plus command. The Minus command will toggle Party Mode "Off". There can also be a discrete
On, Off, or Master command sent.

#### 8.11.1 Party Mode On/Master/Off

NOTE: If Party Mode is Off, then "Plus" will issue a "Master" command. If a Zone is turned on while the rest
of the system has Party Mode On and a different Zone is the Master, then "Plus" from a non-active Zone will
issue an "On" command first and then a "Master" command second. If Party Mode is On and a different Zone is
Master, then "Plus" from an active zone will issue a "Master" command. Minus will always issue an "Off"
command.

The following example shows how to toggle Party Mode for a particular Zone.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 00 00 7F cc zz kk 05 05 02 00 zz 00 07 00 ## 00 00 00 08 00 01 xx F7
```

- Byte #16 = button event (Plus = 0x69 = On/Off toggle) or (Minus = 0x6A = On/Off toggle)

#### 8.11.2 Set Party Mode

Select Party Mode "Master", "On", or "Off" for a particular zone using a discrete message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 00 00 7F cc zz kk 00 05 02 00 zz 00 07 00 00 00 01 00 01 00 ## xx F7
```

- Byte #22 = Party Mode setting (0x00 = OFF, 0x01 = ON, 0x02 = Master)

#### 8.11.3 Get Party Mode

The current Party Mode state for a particular zone can be obtained using the following message.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18
Value F0 cc 00 7F cc zz kk 01 05 02 00 zz 00 07 00 00 xx F7
```

The return message would look like the following.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc zz kk cc 00 7F 00 00 05 02 00 zz 00 07 00 00 01 00 01 00 ## xx F7
```

- Byte #22 = Party Mode setting (0x00 = OFF, 0x01 = ON, 0x02 = Master)

### 8.12 Get All Zone Info

As stated previously, a message can be used to request all of a particular Zone's parameter values at once.
This can be very useful for updating panel displays. The following is an example of how to request Zone
information for a particular Zone and what the return message would look like.

#### 8.12.1 Get State

This is the Request message for the parameter values of the selected Zone.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
Value F0 cc 00 7F cc zz kk 01 04 02 00 zz 07 00 00 xx F7
```

The return message would look like the following.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 cc zz kk cc 00 7F 00 00 04 02 00 zz 07 00 00 01 00 0C 00 ## ## ## ##
Byte  25 26 27 28 29 30 31 32 33 34
Value ## ## ## ## ## ## ## 00 xx F7
```

The parameter values are depicted in bytes 21 - 31. These values will change depending on the state of the
selected Zone. The above example shows the parameter values for a Zone configured and being used as follows:

| Byte | Field |
|---|---|
| 21 | Current Zone On/Off state (0x00 = OFF or 0x01 = ON) |
| 22 | Current Source selected -1 |
| 23 | Current Volume level (0x00 - 0x32, 0x00 = 0 Displayed ... 0x32 = 100 Displayed) |
| 24 | Current Bass level (0x00 = -10 ... 0x0A = Flat ... 0x14 = +10) |
| 25 | Current Treble level (0x00 = -10 ... 0x0A = Flat ... 0x14 = +10) |
| 26 | Current Loudness (0x00 = OFF, 0x01 = ON) |
| 27 | Current Balance level (0x00 = More Left ... 0x0A = Center ... 0x14 = More Right) |
| 28 | Current System On state (0x00 = All Zones Off, 0x01 = Any Zone is On) |
| 29 | Current Shared Source (0x00 = Not Shared, 0x01 = Shared with another Zone) |
| 30 | Current Party Mode state (0x00 = OFF, 0x01 = ON, 0x02 = Master)\* |
| 31 | Current Do Not Disturb state (0x00 = OFF, 0x01 = ON)\* |

\*NOTE: Unsupported Features, information not available for CAS44 and CAA66 controllers.

> **Note (project).** Bytes 19–20 (`0C 00`) appear to be a little-endian data length of 12. That covers
> bytes 21–32, meaning the 11 fields plus the trailing `00`.

## 9 Displaying a String

Since the keypad contains a text display, we've provided a message that will allow you to send text
messages to All Keypads simultaneously or a Specific Keypad individually.

- **"Alignment"**: when sending a message to display text on a Keypad, the first Data byte sets your
  Alignment (0x00 = Centered, 0x01 = Left justified).
- **"Flash Time"**: messages can be displayed constant or they can be flashed on the display for a brief
  time. When sending a message to display text on a Keypad, the second and third Data bytes set the Flash
  Time. Flash Time defines how long the string is displayed. This is measured in 10ms increments with a
  value of 0x00 being constant.
- **"Null character"**: a value of 0x00 used as a Data byte will be a text character of Null. A Null text
  character is always used after the last text character in the display string. With a 12 character text
  message you would use a Null character as the 13th text character. A Null character is also used for
  each unused text character at the end of the text display message if the maximum number of Data bytes
  are not all used.

### 9.1 On All Keypads

The following example shows how to display a text message on All Keypads (or other devices with a text
display) in the system.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 7F 00 00 cc zz kk 00 02 01 01 00 00 00 01 00 10 00 ## ## ## ## ## ##
Byte  25 26 27 28 29 30 31 32 33 34 35 36
Value ## ## ## ## ## ## ## ## ## ## xx F7
```

- Byte #19 = Alignment (0x00 = Centered, 0x01 = Left justified)
- Byte #20 = Low Byte of Flash Time
- Byte #21 = High Byte of Flash Time
- Byte #22 - #34 = ASCII text characters (Section 11 ASCII Character Set)

> **Erratum (minor).** The ASCII chart is section **12**, not 11.

### 9.2 On A Specific Keypad

The following example shows how to display a text message on a Specific Keypad in the system. This is
accomplished by setting the Target Device ID for the particular keypad in question.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 tc tz tk cc zz kk 00 02 01 01 00 00 00 01 00 10 00 ## ## ## ## ## ##
Byte  25 26 27 28 29 30 31 32 33 34 35 36
Value ## ## ## ## ## ## ## ## ## ## xx F7
```

- tc = Target Controller Number -1
- tz = Target Zone Number -1
- tk = Target Keypad Number -1
- Byte #19 = Alignment (0x00 = Centered, 0x01 = Left justified)
- Byte #20 = Low Byte of Flash Time
- Byte #21 = High Byte of Flash Time
- Byte #22 - #34 = ASCII text characters (Section 11 ASCII Character Set)

## 10 Display Messages

The following section describes how to read Display Messages. These include Direct Display Feedback
messages, Source Broadcast Display Feedback messages, and Multi-Field Broadcast Display Feedback messages.

NOTE: In order to fully support the display capabilities, Direct Display feedback messages, Source Broadcast
Feedback messages and Multi-Field Broadcast Messages, should be supported. (Multi-Field Broadcast messages
are only used with XM.)

### 10.1 Reading Direct Display Feedback

This section describes how to read Direct Display Feedback Messages. These Feedback messages are usually sent
in direct response to a received command (e.g., If the current frequency of a Russound tuner is "102.7 MHz
FM", sending the "Frequency Up" command will trigger the Tuner to send a Display message back to the sender
to update the frequency Display to "102.9 MHz FM"). The Direct Display Feedback message is sent directly to
the Target Device ID of the message sender. The message can be displayed for a constant amount of time, or a
"Flash" display with a specified length of time (Flash Time is in increments of 10ms).

NOTE: It is possible that some display messages will include the special "Invert" control character (0xF1).

NOTE: Some of the other bytes within this message may vary. Only the ones necessary to interpret the message
are highlighted.

NOTE: This message shows a text payload of 16 characters. Some devices may have a text payload of 12
characters.

This is what the Direct Display Feedback message would look like using the above example.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 00 00 70 00 7D 00 00 02 01 01 02 01 01 00 00 01 00 14 00 01 00 00 31
Byte  25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42
Value 30 32 2E 39 20 4D 48 7A 20 46 4D 00 00 00 00 00 7F F7
```

- Byte #2 - #4 = Target Device ID (This message should be displayed if the target Device ID matches the
  Device ID of your device.)
- Byte #19 = Overall Payload Size
- Byte #22 = Flash Time low byte (Flash time is in 10ms increments, 0x00 = Constant)
- Byte #23 = Flash Time high byte
- Byte #24 - #40 = Text ("102.9 MHz FM" used in above example)
- Byte #41 = Calculated Checksum

### 10.2 Reading Source Broadcast Display Feedback

This section describes how to read Source Broadcast Display Feedback messages. These Feedback messages are
sent to update all devices monitoring a given Source's status. These messages may be sent as a direct result
of a sent command or as a general update. The Display Feedback message is sent with the source number of the
Source attached. The attached source number indicates which Source the update is intended. The message can
be displayed for a constant amount of time, or a "Flash" display with a specified length of time (Flash Time
is in increments of 10ms).

NOTE: It is possible that some display messages will include the special "Invert" control character (0xF1).

NOTE: Some of the other bytes within this message may vary. Only the ones necessary to interpret the message
are highlighted.

NOTE: This message shows a text payload of 16 characters. Some devices may have a text payload of 12
characters.

This example shows a Source Broadcast Display Feedback message with text "102.9 MHz FM".

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 7D 00 79 00 7D 00 00 02 01 01 02 01 01 00 00 01 00 14 00 10 00 00 31
Byte  25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42
Value 30 32 2E 39 20 4D 48 7A 20 46 4D 00 00 00 00 00 14 F7
```

- Byte #4 = Target Keypad ID (NOTE: A value of 0x79 in the Target Keypad ID field indicates that this
  message is a Source Broadcast Display Feedback message.)
- Byte #19 = Overall Payload Size
- Byte #21 = Message type and Source Number = (0x10 (Source Broadcast Display Type) bit-wise OR-ed with the
  source number (e.g. source 1 = 0x10, source 3 = 0x12))
- Byte #22 = Flash Time low byte (Flash time is in 10ms increments, 0x00 = Constant)
- Byte #23 = Flash Time high byte
- Bytes #24 - #40 = Text ("102.9 MHz FM")
- Byte #41 = Calculated Checksum

### 10.3 Reading Multi-Field Broadcast Display Feedback Messages

Multi-Field Broadcast Display Feedback messages are sent to update all devices monitoring the Source's
status. These Feedback messages are sent to update all devices monitoring a given Source's status. These
messages may be sent as a direct result of a sent command or as a general update. The Display Feedback
message is sent with the source number of the Source attached. A "Field ID" is included with the message to
indicate which item is being updated (see table below). The message can be displayed for a constant amount
of time, or a "Flash" display with a specified length of time (Flash Time is in increments of 10ms).

This example shows a Multi-Field Broadcast Display Feedback message text "49: Fine Tuning".

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
Value F0 7D 00 79 00 7D 00 00 02 01 01 02 01 01 00 00 01 00 14 00 20 07 1C 34
Byte  25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42
Value 39 3A 46 69 6E 65 54 75 6E 69 6E 67 00 00 00 00 09 F7
```

- Byte #4 = Target Keypad ID (NOTE: A value of 0x79 in the Target Keypad ID field indicates that this
  message is a Source Broadcast Display Feedback message.)
- Byte #19 = Overall Payload Size
- Byte #21 = Message type and Source Number = (0x20 (Multi-Field Display Type) bit-wise OR-ed with the
  source number (e.g., source 1 = 0x20, source 3 = 0x22.))
- Byte #22 = Field ID bit and Flash Bit = (bits 0 - 6 = Field Id (i.e., 0x07 = 7 = Channel Name), Bit 7 -
  Ignore. Invert control character (0xF1) will be inserted before this byte if Bit 7 is set.
- Bytes #24 - #40 = Text ("49: Fine Tuning")
- Byte #41 = Calculated Checksum

> **Erratum (omission).** The text says "see table below", but the PDF contains no Field ID table.

## 11 Using the Acknowledge Message (Handshaking)

When the controller sends a Return message in response to a Request message, the sender of the Request
message must send an Acknowledge message in response to the Return message. Failure to send the Acknowledge
message will result in a system delay of approximately 2.5 seconds while the controller tries to re-send the
data. The controller will also send an Acknowledge message in response to a Set Data message that it is sent.
It is not necessary to process the incoming Acknowledge message from the controller.

NOTE: A Peripheral Source does not require Handshaking when the keypad ID used to send data is 0x70. If using
a keypad ID of 0x70, and therefore no Handshaking, it is best to leave approximately 100ms between messages
to ensure that all messages are processed correctly. If Handshaking is desired, it is recommended to use a
keypad ID of 0x60. In this case, the Peripheral Source will send an Acknowledge message in response to any
high priority event messages sent. Acknowledge messages can be (and normally are) used to trigger the release
of any additional messages that may be in the sender's queue.

This example shows sending a Frequency Up (Tune Up) event to a Russound Tuner: In this case the event
priority byte is set to 0x00 (high) and the sender Keypad ID is 0x60. This means that the Internal Tuner will
send an Acknowledge message in response.

```
Byte   1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
Value F0 00 7D 00 00 00 60 05 02 01 00 02 01 00 2F 00 70 00 00 00 00 0C F7
```

- Byte #4 = Source Number - 1 (Zero-based)
- Byte #7 = Sender Keypad ID - 0x60 (0x60 indicates that this message requires a handshake message if the
  event is high priority)
- Byte #15 = Event ID (TUNE UP)
- Byte #17 = Return Keypad Id - 0x70 (This is the keypad ID that the ST2 will send any resulting direct
  display message to. If this byte is set to 0x70, it will not require you to send a handshake message in
  response to the display message. If the original Sender keypad ID of 0x60 is used here, you will need to
  send a handshake message when the direct display message comes back. Failure to send the handshake
  message will result in a system delay of approximately 2.5 seconds, while the ST2 tries to re-send the
  data. It is recommended to leave this byte as 0x70.)
- Byte #19 = Source Number - 1 (Zero-based)
- Byte #21 = Event Priority (0 = high, 1 = low)
- Byte #22 = Calculated Checksum

Handshake message returned to sender to acknowledge a high priority event message:

```
Byte   1  2  3  4  5  6  7  8  9 10 11
Value F0 00 00 60 00 7D 00 02 06 5E F7
```

- Byte #7 = Source Number of Event Recipient - 1 (Zero-based)
- Byte #8 = Message Type (HANDSHAKE)
- Byte #9 = Handshake Type (EVENT HANDSHAKE)
- Byte #10 = Calculated Checksum

> **Note (project).** This is the only handshake frame in the document, and it is an *event* handshake
> (type `06`). The *data* handshake that §7 requires after a controller's Set Data reply is never shown.
> The keypad-ID-0x70 note above refers to Peripheral Sources. Whether it also means the controller skips
> handshaking for a 0x70 sender is unknown and is to be tested in M2 (SPEC §11).

## 12 ASCII Character Set to HEX Conversion Chart

Standard printable ASCII, 0x20–0x7F:

| HEX | ASCII | HEX | ASCII | HEX | ASCII |
|---|---|---|---|---|---|
| 20 | Space | 40 | `@` | 60 | `` ` `` |
| 21 | `!` | 41 | `A` | 61 | `a` |
| 22 | `"` | 42 | `B` | 62 | `b` |
| 23 | `#` | 43 | `C` | 63 | `c` |
| 24 | `$` | 44 | `D` | 64 | `d` |
| 25 | `%` | 45 | `E` | 65 | `e` |
| 26 | `&` | 46 | `F` | 66 | `f` |
| 27 | `'` | 47 | `G` | 67 | `g` |
| 28 | `(` | 48 | `H` | 68 | `h` |
| 29 | `)` | 49 | `I` | 69 | `i` |
| 2A | `*` | 4A | `J` | 6A | `j` |
| 2B | `+` | 4B | `K` | 6B | `k` |
| 2C | `,` | 4C | `L` | 6C | `l` |
| 2D | `-` | 4D | `M` | 6D | `m` |
| 2E | `.` | 4E | `N` | 6E | `n` |
| 2F | `/` | 4F | `O` | 6F | `o` |
| 30 | `0` | 50 | `P` | 70 | `p` |
| 31 | `1` | 51 | `Q` | 71 | `q` |
| 32 | `2` | 52 | `R` | 72 | `r` |
| 33 | `3` | 53 | `S` | 73 | `s` |
| 34 | `4` | 54 | `T` | 74 | `t` |
| 35 | `5` | 55 | `U` | 75 | `u` |
| 36 | `6` | 56 | `V` | 76 | `v` |
| 37 | `7` | 57 | `W` | 77 | `w` |
| 38 | `8` | 58 | `X` | 78 | `x` |
| 39 | `9` | 59 | `Y` | 79 | `y` |
| 3A | `:` | 5A | `Z` | 7A | `z` |
| 3B | `;` | 5B | `[` | 7B | `{` |
| 3C | `<` | 5C | `\` | 7C | \| |
| 3D | `=` | 5D | `]` | 7D | `}` |
| 3E | `>` | 5E | `^` | 7E | `~` |
| 3F | `?` | 5F | `_` | 7F | Del |

> **Erratum (minor).** The PDF chart prints 0x2C as `‘` (a quotation mark). 0x2C is a comma.

## 13 Russound Controllers RS-232 Hex Code Listing

The PDF's hex tables (pp. 48–51), regenerated row by row. Each row's checksum was recomputed and matches
the value printed in the PDF.

> **Note (project).** The Zone On/Off rows use Source Zone ID (byte 6) `00` for every zone. Every other
> table varies byte 6 with the zone. See the note under §8.1.1.

### Controller #1 keypad events: zone power on/off

| Command | Frame |
|---|---|
| Zone 1 On | `F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 01 00 00 00 01 12 F7` |
| Zone 1 Off | `F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 00 00 00 00 01 11 F7` |
| Zone 2 On | `F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 01 00 01 00 01 13 F7` |
| Zone 2 Off | `F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 00 00 01 00 01 12 F7` |
| Zone 3 On | `F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 01 00 02 00 01 14 F7` |
| Zone 3 Off | `F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 00 00 02 00 01 13 F7` |
| Zone 4 On | `F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 01 00 03 00 01 15 F7` |
| Zone 4 Off | `F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 00 00 03 00 01 14 F7` |
| Zone 5 On | `F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 01 00 04 00 01 16 F7` |
| Zone 5 Off | `F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 00 00 04 00 01 15 F7` |
| Zone 6 On | `F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 01 00 05 00 01 17 F7` |
| Zone 6 Off | `F0 00 00 7F 00 00 70 05 02 02 00 00 F1 23 00 00 00 05 00 01 16 F7` |

### Controller #1: all on / all off

| Command | Frame |
|---|---|
| All On | `F0 7E 00 7F 00 00 70 05 02 02 00 00 F1 22 00 01 00 00 00 01 0F F7` |
| All Off | `F0 7E 00 7F 00 00 70 05 02 02 00 00 F1 22 00 00 00 00 00 01 0E F7` |

### Controller #1 keypad events: zone volume up/down

| Command | Frame |
|---|---|
| Zone 1 Vol Up | `F0 00 00 7F 00 00 70 05 02 02 00 00 7F 00 00 00 00 00 01 7B F7` |
| Zone 1 Vol Down | `F0 00 00 7F 00 00 70 05 02 02 00 00 F1 7F 00 00 00 00 00 01 6D F7` |
| Zone 2 Vol Up | `F0 00 00 7F 00 01 70 05 02 02 00 00 7F 00 00 00 00 00 01 7C F7` |
| Zone 2 Vol Down | `F0 00 00 7F 00 01 70 05 02 02 00 00 F1 7F 00 00 00 00 00 01 6E F7` |
| Zone 3 Vol Up | `F0 00 00 7F 00 02 70 05 02 02 00 00 7F 00 00 00 00 00 01 7D F7` |
| Zone 3 Vol Down | `F0 00 00 7F 00 02 70 05 02 02 00 00 F1 7F 00 00 00 00 00 01 6F F7` |
| Zone 4 Vol Up | `F0 00 00 7F 00 03 70 05 02 02 00 00 7F 00 00 00 00 00 01 7E F7` |
| Zone 4 Vol Down | `F0 00 00 7F 00 03 70 05 02 02 00 00 F1 7F 00 00 00 00 00 01 70 F7` |
| Zone 5 Vol Up | `F0 00 00 7F 00 04 70 05 02 02 00 00 7F 00 00 00 00 00 01 7F F7` |
| Zone 5 Vol Down | `F0 00 00 7F 00 04 70 05 02 02 00 00 F1 7F 00 00 00 00 00 01 71 F7` |
| Zone 6 Vol Up | `F0 00 00 7F 00 05 70 05 02 02 00 00 7F 00 00 00 00 00 01 00 F7` |
| Zone 6 Vol Down | `F0 00 00 7F 00 05 70 05 02 02 00 00 F1 7F 00 00 00 00 00 01 72 F7` |

### Controller #1 keypad events: zone previous/next and plus/minus

| Command | Frame |
|---|---|
| Zone 1 Prev | `F0 00 00 7F 00 00 70 05 02 02 00 00 67 00 00 00 00 00 01 63 F7` |
| Zone 1 Next | `F0 00 00 7F 00 00 70 05 02 02 00 00 68 00 00 00 00 00 01 64 F7` |
| Zone 1 Plus | `F0 00 00 7F 00 00 70 05 02 02 00 00 69 00 00 00 00 00 01 65 F7` |
| Zone 1 Minus | `F0 00 00 7F 00 00 70 05 02 02 00 00 6A 00 00 00 00 00 01 66 F7` |
| Zone 2 Prev | `F0 00 00 7F 00 01 70 05 02 02 00 00 67 00 00 00 00 00 01 64 F7` |
| Zone 2 Next | `F0 00 00 7F 00 01 70 05 02 02 00 00 68 00 00 00 00 00 01 65 F7` |
| Zone 2 Plus | `F0 00 00 7F 00 01 70 05 02 02 00 00 69 00 00 00 00 00 01 66 F7` |
| Zone 2 Minus | `F0 00 00 7F 00 01 70 05 02 02 00 00 6A 00 00 00 00 00 01 67 F7` |
| Zone 3 Prev | `F0 00 00 7F 00 02 70 05 02 02 00 00 67 00 00 00 00 00 01 65 F7` |
| Zone 3 Next | `F0 00 00 7F 00 02 70 05 02 02 00 00 68 00 00 00 00 00 01 66 F7` |
| Zone 3 Plus | `F0 00 00 7F 00 02 70 05 02 02 00 00 69 00 00 00 00 00 01 67 F7` |
| Zone 3 Minus | `F0 00 00 7F 00 02 70 05 02 02 00 00 6A 00 00 00 00 00 01 68 F7` |
| Zone 4 Prev | `F0 00 00 7F 00 03 70 05 02 02 00 00 67 00 00 00 00 00 01 66 F7` |
| Zone 4 Next | `F0 00 00 7F 00 03 70 05 02 02 00 00 68 00 00 00 00 00 01 67 F7` |
| Zone 4 Plus | `F0 00 00 7F 00 03 70 05 02 02 00 00 69 00 00 00 00 00 01 68 F7` |
| Zone 4 Minus | `F0 00 00 7F 00 03 70 05 02 02 00 00 6A 00 00 00 00 00 01 69 F7` |
| Zone 5 Prev | `F0 00 00 7F 00 04 70 05 02 02 00 00 67 00 00 00 00 00 01 67 F7` |
| Zone 5 Next | `F0 00 00 7F 00 04 70 05 02 02 00 00 68 00 00 00 00 00 01 68 F7` |
| Zone 5 Plus | `F0 00 00 7F 00 04 70 05 02 02 00 00 69 00 00 00 00 00 01 69 F7` |
| Zone 5 Minus | `F0 00 00 7F 00 04 70 05 02 02 00 00 6A 00 00 00 00 00 01 6A F7` |
| Zone 6 Prev | `F0 00 00 7F 00 05 70 05 02 02 00 00 67 00 00 00 00 00 01 68 F7` |
| Zone 6 Next | `F0 00 00 7F 00 05 70 05 02 02 00 00 68 00 00 00 00 00 01 69 F7` |
| Zone 6 Plus | `F0 00 00 7F 00 05 70 05 02 02 00 00 69 00 00 00 00 00 01 6A F7` |
| Zone 6 Minus | `F0 00 00 7F 00 05 70 05 02 02 00 00 6A 00 00 00 00 00 01 6B F7` |

### Controller #1 keypad events: zone play/stop/pause/F1/F2

| Command | Frame |
|---|---|
| Zone 1 Play | `F0 00 00 7F 00 00 70 05 02 02 00 00 73 00 00 00 00 00 01 6F F7` |
| Zone 1 Stop | `F0 00 00 7F 00 00 70 05 02 02 00 00 6D 00 00 00 00 00 01 69 F7` |
| Zone 1 Pause | `F0 00 00 7F 00 00 70 05 02 02 00 00 6E 00 00 00 00 00 01 6A F7` |
| Zone 1 F1 | `F0 00 00 7F 00 00 70 05 02 02 00 00 6F 00 00 00 00 00 01 6B F7` |
| Zone 1 F2 | `F0 00 00 7F 00 00 70 05 02 02 00 00 70 00 00 00 00 00 01 6C F7` |
| Zone 2 Play | `F0 00 00 7F 00 01 70 05 02 02 00 00 73 00 00 00 00 00 01 70 F7` |
| Zone 2 Stop | `F0 00 00 7F 00 01 70 05 02 02 00 00 6D 00 00 00 00 00 01 6A F7` |
| Zone 2 Pause | `F0 00 00 7F 00 01 70 05 02 02 00 00 6E 00 00 00 00 00 01 6B F7` |
| Zone 2 F1 | `F0 00 00 7F 00 01 70 05 02 02 00 00 6F 00 00 00 00 00 01 6C F7` |
| Zone 2 F2 | `F0 00 00 7F 00 01 70 05 02 02 00 00 70 00 00 00 00 00 01 6D F7` |
| Zone 3 Play | `F0 00 00 7F 00 02 70 05 02 02 00 00 73 00 00 00 00 00 01 71 F7` |
| Zone 3 Stop | `F0 00 00 7F 00 02 70 05 02 02 00 00 6D 00 00 00 00 00 01 6B F7` |
| Zone 3 Pause | `F0 00 00 7F 00 02 70 05 02 02 00 00 6E 00 00 00 00 00 01 6C F7` |
| Zone 3 F1 | `F0 00 00 7F 00 02 70 05 02 02 00 00 6F 00 00 00 00 00 01 6D F7` |
| Zone 3 F2 | `F0 00 00 7F 00 02 70 05 02 02 00 00 70 00 00 00 00 00 01 6E F7` |
| Zone 4 Play | `F0 00 00 7F 00 03 70 05 02 02 00 00 73 00 00 00 00 00 01 72 F7` |
| Zone 4 Stop | `F0 00 00 7F 00 03 70 05 02 02 00 00 6D 00 00 00 00 00 01 6C F7` |
| Zone 4 Pause | `F0 00 00 7F 00 03 70 05 02 02 00 00 6E 00 00 00 00 00 01 6D F7` |
| Zone 4 F1 | `F0 00 00 7F 00 03 70 05 02 02 00 00 6F 00 00 00 00 00 01 6E F7` |
| Zone 4 F2 | `F0 00 00 7F 00 03 70 05 02 02 00 00 70 00 00 00 00 00 01 6F F7` |
| Zone 5 Play | `F0 00 00 7F 00 04 70 05 02 02 00 00 73 00 00 00 00 00 01 73 F7` |
| Zone 5 Stop | `F0 00 00 7F 00 04 70 05 02 02 00 00 6D 00 00 00 00 00 01 6D F7` |
| Zone 5 Pause | `F0 00 00 7F 00 04 70 05 02 02 00 00 6E 00 00 00 00 00 01 6E F7` |
| Zone 5 F1 | `F0 00 00 7F 00 04 70 05 02 02 00 00 6F 00 00 00 00 00 01 6F F7` |
| Zone 5 F2 | `F0 00 00 7F 00 04 70 05 02 02 00 00 70 00 00 00 00 00 01 70 F7` |
| Zone 6 Play | `F0 00 00 7F 00 05 70 05 02 02 00 00 73 00 00 00 00 00 01 74 F7` |
| Zone 6 Stop | `F0 00 00 7F 00 05 70 05 02 02 00 00 6D 00 00 00 00 00 01 6E F7` |
| Zone 6 Pause | `F0 00 00 7F 00 05 70 05 02 02 00 00 6E 00 00 00 00 00 01 6F F7` |
| Zone 6 F1 | `F0 00 00 7F 00 05 70 05 02 02 00 00 6F 00 00 00 00 00 01 70 F7` |
| Zone 6 F2 | `F0 00 00 7F 00 05 70 05 02 02 00 00 70 00 00 00 00 00 01 71 F7` |

### Controller #1 keypad events: zone source select

| Command | Frame |
|---|---|
| Zone 1 Src 1 | `F0 00 00 7F 00 00 70 05 02 00 00 00 F1 3E 00 00 00 00 00 01 2A F7` |
| Zone 1 Src 2 | `F0 00 00 7F 00 00 70 05 02 00 00 00 F1 3E 00 00 00 01 00 01 2B F7` |
| Zone 1 Src 3 | `F0 00 00 7F 00 00 70 05 02 00 00 00 F1 3E 00 00 00 02 00 01 2C F7` |
| Zone 1 Src 4 | `F0 00 00 7F 00 00 70 05 02 00 00 00 F1 3E 00 00 00 03 00 01 2D F7` |
| Zone 1 Src 5 | `F0 00 00 7F 00 00 70 05 02 00 00 00 F1 3E 00 00 00 04 00 01 2E F7` |
| Zone 1 Src 6 | `F0 00 00 7F 00 00 70 05 02 00 00 00 F1 3E 00 00 00 05 00 01 2F F7` |
| Zone 1 Aux | `F0 00 00 7F 00 00 70 05 02 00 00 00 F1 3E 00 00 00 06 00 01 30 F7` |
| Zone 2 Src 1 | `F0 00 00 7F 00 01 70 05 02 00 00 00 F1 3E 00 00 00 00 00 01 2B F7` |
| Zone 2 Src 2 | `F0 00 00 7F 00 01 70 05 02 00 00 00 F1 3E 00 00 00 01 00 01 2C F7` |
| Zone 2 Src 3 | `F0 00 00 7F 00 01 70 05 02 00 00 00 F1 3E 00 00 00 02 00 01 2D F7` |
| Zone 2 Src 4 | `F0 00 00 7F 00 01 70 05 02 00 00 00 F1 3E 00 00 00 03 00 01 2E F7` |
| Zone 2 Src 5 | `F0 00 00 7F 00 01 70 05 02 00 00 00 F1 3E 00 00 00 04 00 01 2F F7` |
| Zone 2 Src 6 | `F0 00 00 7F 00 01 70 05 02 00 00 00 F1 3E 00 00 00 05 00 01 30 F7` |
| Zone 2 Aux | `F0 00 00 7F 00 01 70 05 02 00 00 00 F1 3E 00 00 00 06 00 01 31 F7` |
| Zone 3 Src 1 | `F0 00 00 7F 00 02 70 05 02 00 00 00 F1 3E 00 00 00 00 00 01 2C F7` |
| Zone 3 Src 2 | `F0 00 00 7F 00 02 70 05 02 00 00 00 F1 3E 00 00 00 01 00 01 2D F7` |
| Zone 3 Src 3 | `F0 00 00 7F 00 02 70 05 02 00 00 00 F1 3E 00 00 00 02 00 01 2E F7` |
| Zone 3 Src 4 | `F0 00 00 7F 00 02 70 05 02 00 00 00 F1 3E 00 00 00 03 00 01 2F F7` |
| Zone 3 Src 5 | `F0 00 00 7F 00 02 70 05 02 00 00 00 F1 3E 00 00 00 04 00 01 30 F7` |
| Zone 3 Src 6 | `F0 00 00 7F 00 02 70 05 02 00 00 00 F1 3E 00 00 00 05 00 01 31 F7` |
| Zone 3 Aux | `F0 00 00 7F 00 02 70 05 02 00 00 00 F1 3E 00 00 00 06 00 01 32 F7` |
| Zone 4 Src 1 | `F0 00 00 7F 00 03 70 05 02 00 00 00 F1 3E 00 00 00 00 00 01 2D F7` |
| Zone 4 Src 2 | `F0 00 00 7F 00 03 70 05 02 00 00 00 F1 3E 00 00 00 01 00 01 2E F7` |
| Zone 4 Src 3 | `F0 00 00 7F 00 03 70 05 02 00 00 00 F1 3E 00 00 00 02 00 01 2F F7` |
| Zone 4 Src 4 | `F0 00 00 7F 00 03 70 05 02 00 00 00 F1 3E 00 00 00 03 00 01 30 F7` |
| Zone 4 Src 5 | `F0 00 00 7F 00 03 70 05 02 00 00 00 F1 3E 00 00 00 04 00 01 31 F7` |
| Zone 4 Src 6 | `F0 00 00 7F 00 03 70 05 02 00 00 00 F1 3E 00 00 00 05 00 01 32 F7` |
| Zone 4 Aux | `F0 00 00 7F 00 03 70 05 02 00 00 00 F1 3E 00 00 00 06 00 01 33 F7` |
| Zone 5 Src 1 | `F0 00 00 7F 00 04 70 05 02 00 00 00 F1 3E 00 00 00 00 00 01 2E F7` |
| Zone 5 Src 2 | `F0 00 00 7F 00 04 70 05 02 00 00 00 F1 3E 00 00 00 01 00 01 2F F7` |
| Zone 5 Src 3 | `F0 00 00 7F 00 04 70 05 02 00 00 00 F1 3E 00 00 00 02 00 01 30 F7` |
| Zone 5 Src 4 | `F0 00 00 7F 00 04 70 05 02 00 00 00 F1 3E 00 00 00 03 00 01 31 F7` |
| Zone 5 Src 5 | `F0 00 00 7F 00 04 70 05 02 00 00 00 F1 3E 00 00 00 04 00 01 32 F7` |
| Zone 5 Src 6 | `F0 00 00 7F 00 04 70 05 02 00 00 00 F1 3E 00 00 00 05 00 01 33 F7` |
| Zone 5 Aux | `F0 00 00 7F 00 04 70 05 02 00 00 00 F1 3E 00 00 00 06 00 01 34 F7` |
| Zone 6 Src 1 | `F0 00 00 7F 00 05 70 05 02 00 00 00 F1 3E 00 00 00 00 00 01 2F F7` |
| Zone 6 Src 2 | `F0 00 00 7F 00 05 70 05 02 00 00 00 F1 3E 00 00 00 01 00 01 30 F7` |
| Zone 6 Src 3 | `F0 00 00 7F 00 05 70 05 02 00 00 00 F1 3E 00 00 00 02 00 01 31 F7` |
| Zone 6 Src 4 | `F0 00 00 7F 00 05 70 05 02 00 00 00 F1 3E 00 00 00 03 00 01 32 F7` |
| Zone 6 Src 5 | `F0 00 00 7F 00 05 70 05 02 00 00 00 F1 3E 00 00 00 04 00 01 33 F7` |
| Zone 6 Src 6 | `F0 00 00 7F 00 05 70 05 02 00 00 00 F1 3E 00 00 00 05 00 01 34 F7` |
| Zone 6 Aux | `F0 00 00 7F 00 05 70 05 02 00 00 00 F1 3E 00 00 00 06 00 01 35 F7` |

## Appendix A: Errata index

Added by the RNET Bridge project. "Suspected" means the item is inferred from the document's own
consistency and still needs hardware confirmation.

| § | Item | Status |
|---|---|---|
| Quick Ref | "Displaying a String" page number is 41, not 40 | Minor |
| 2 | Set/Get messages are in section 8, not 7 | Minor |
| 3.1 | Data bits not stated; 8N1 | Omission |
| 6.2 | Zone 3 Record checksum printed `49`, correct `4F` | Confirmed (arithmetic) |
| 6.2 | Zone 1 Menu break-out checksum printed `7B`, correct `4E` | Confirmed (arithmetic) |
| 8.1.1 vs 13 | Zone On/Off Source Zone ID: `zz` in the template, `00` in the hex table | Open (verify in M2) |
| 8.3.2 | Get Volume reply "Volume Level -1" | Suspected |
| 8.8.1 | Turn On Volume Plus/Minus described as "More Left/More Right" | Confirmed (copy-paste) |
| 8.8.3 | Turn On Volume reply path copied from Get Volume | Suspected (verify in M2) |
| 9.1, 9.2 | ASCII chart is section 12, not 11 | Minor |
| 10.3 | Referenced Field ID table is missing | Omission |
| 11 | Data-handshake (ACK) frame format not given | Open (verify in M2) |
| 12 | 0x2C rendered as `‘`; it is a comma | Minor |

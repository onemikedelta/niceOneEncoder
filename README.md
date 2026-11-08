# niceOneEncoder

Encoder for the Nice One rolling code protocol without a rainbow table.

Sends a **72-bit Nice One** frame (a 52-bit Nice Flor-S core plus a 20-bit
extension) or a plain **52-bit Nice Flor-S** frame, using OOK on a single
digital pin. Four buttons map to the four Nice button codes.

The cipher is computed from a 32-byte key, so no lookup table is needed and the
sketch fits comfortably on an ATmega328P with no external memory.

## Hardware

| Signal | Default pin | Notes |
|---|---|---|
| Transmitter data | D8 | OOK/ASK transmitter data input |
| LED | D13 | `LED_BUILTIN`, lit while transmitting |
| Buttons 1-4 | D9, D10, D11, D12 | wired to GND, internal pull-ups |

Written for the ATmega328P at 16 MHz. Other AVRs work if `delayMicroseconds()`
is accurate at the clock in use.

## Configuration

Edit the top of `niceOneEncoder.ino`:

```c
#define REMOTE_SERIAL 0x1234567UL   /* 28-bit remote serial */
#define FRAME_BITS    72            /* 72 = Nice One, 52 = Nice Flor-S */
```

The rolling counter is kept in the internal EEPROM and advances by one per
button press. A receiver only accepts a counter higher than the last one it
saw, so if the counter is ever lost, set it above the last transmitted value.

## Protocol

```
[ ---------- 52 bits: Nice Flor-S ---------- ][ -- 20 bits: Nice One -- ]
  P0  P1  P2  P3  P4  P5  P6  P7               P8   hold   CRC8
```

* `P0` button code, positional: 1, 2, 4, 8
* `P1` `0x0F ^ P0 ^ parcel`, where parcel counts 0..15 within one press
* `P2`..`P7` the encrypted body, derived from serial and counter
* `P8`, hold flag and CRC8 form the Nice One extension

Timing: bit 1 is 1000 us high + 500 us low, bit 0 is 500 us high + 1000 us low,
start and stop bits are 1500 us, and each parcel is preceded by 18.5 ms of
silence. Sixteen parcels are sent per press.

## License

Copyright (c) 2026 [onemikedelta](https://github.com/onemikedelta)

Shared for education and research under the **niceOneEncoder License 1.0** -
non-commercial copyleft. See [LICENSE](LICENSE) for the full text.

* **Use it freely, no need to ask.** Private, educational and research use,
  and adding it to your own project, require no prior permission.
* **Keep the source open.** If you distribute this or anything built on it,
  the complete source of what you distributed must be publicly available,
  under this same license. Binary-only releases are not allowed.
* **No commercial use without permission.** Selling it, selling hardware that
  contains it, or using it in a paid product or service requires prior written
  permission from the author. Open an issue to ask.
* **Credit the author.** Keep the copyright notice, credit
  [onemikedelta](https://github.com/onemikedelta), link back to this
  repository, and state what you changed.

This is not an OSI-approved open source license, because it restricts
commercial use.

The software is provided as-is, without warranty of any kind. You are
responsible for complying with your local radio regulations and for using this
only with equipment you own or are authorised to operate.

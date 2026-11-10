# Gold transport development checkpoint

Gold/History crossplay is not implemented. The new Gold packet decoder is a
passive development component, has no caller in the local service, and opens
no socket. Gold remains excluded from normal multiplayer admission.

## Independently mapped differences

The fingerprinted Gold 1.12.1396 executable uses the older Quazal transport.
Its packet writer at RVA `0x91d1f0` writes source/destination virtual ports,
type/flags, session, a four-byte packet signature and a two-byte sequence in
the first ten bytes. SYN/CONNECT add a four-byte connection signature; DATA
adds a one-byte fragment index. Flag `0x40` adds a two-byte payload length.
The final transport checksum is **one byte**, unlike the four-byte checksum
in the independently mapped History transport. These checksums do not
authenticate players or replace simulation state checksums.

`GoldPrudpChecksum` reproduces Gold's function at RVA `0x8456e0`: add
little-endian 32-bit words with 32-bit wrapping, fold the resulting four
bytes, then add remaining bytes and the explicit stream seed modulo 256.
A simple sum of every input byte gives different results after word overflow.
The live stream seed and the Gold connection/bootstrap contract are still
unverified; the decoder requires a caller-supplied seed and assumes no default.

`GoldPrudpPacketDecoder` accepts only a bounded single packet and returns
metadata and a payload slice. It requires the checksum and exact declared
length where present. Unsupported streams, packet types and the unmapped
`0x80` extension are rejected. Crypto, retransmission, fragmentation assembly,
coalesced packets, RMC, tickets, identity and connection state are not supplied.
There is no fallback from a failed History decode to this Gold codec.

## Verification

The source-only oracle executes the exact original checksum routine in an
emulated x86 CPU with synthetic inputs. It never calls native game code in
the host process. The executable hash is mandatory; execution time,
instruction count and writes are bounded. Stack cleanup, nonvolatile
registers and unchanged inputs are checked. A new output path under `work/`
is required. Install developer dependencies `pefile` and Unicorn 2.1.4 in a
private environment, then run:

```console
python tools/gold/emulate_checksum.py <owned-original-Gold-exe> work/checksum.json
```

All 504 comparisons passed, including empty input, every remainder, word
overflow and a 65,507-byte input. The C# tests contain 126 short synthetic
vectors from that native oracle, plus handcrafted header and rejection cases.
They passed 1,782 checks and 10,000 malformed-input samples. The unchanged
History decoder regression passed 2,871 checks and 10,000 malformed samples;
the main Debug service build passed without warnings. These are emulation,
codec and build results, not live packet or multiplayer observations.

## Cross-edition findings and next gate

Read-only archive comparison found 720 of 732 shared resource payloads and
797 of 800 map payloads byte-identical. Two maps contain differing payloads;
those differences must be resolved before sharing a content identity.
Matching native visitor traces for start/turn/checksum events are encouraging,
but do not establish equal command wire bytes or deterministic simulation.
The History storm connectivity facade is absent from Gold, and several
native session/transport vtables have different slot counts. Reusing its
addresses or admitting Gold under the History profile cannot provide crossplay.

The next gate is an independently guarded Gold loopback bootstrap with genuine
local connection callbacks, followed by Gold-to-Gold gameplay. Only then can
Gold/Ubisoft History/Steam History be tested in both host directions for
commands, sustained native checksums, castles, AI, content and save/resume.
See the [sanitized receipt](verification/gold-transport-20261110.json).

#!/usr/bin/env python3
"""
PC-side UART receiver for SoC audio recorder.

Two modes are supported, matching the two firmware paths in main.c:

  Recording mode (RUN_UART_SAMPLE_WAV_FLASH_TEST = 0)
      FPGA may send:  "ACRC" + status + len + source/flash CRC fields
      PC prints the Flash write/readback comparison and exits.

      Legacy full-readback mode:
      FPGA sends:  "WAV0" + len(4B LE) + WAV bytes
      PC receives the bytes and writes a .wav file.

  Test mode / file flash loopback
      FPGA sends:  "WVT0" + len + flash_addr + page_bytes
      FPGA sends:  repeated "WVRQ" + offset + chunk_len
      PC replies:  chunk_len bytes read from the sample file
      FPGA sends:  "WVRS" + status + len_or_fail_offset + crc_source + crc_flash
      FPGA sends:  "WAV0" + len + bytes read back from SPI flash

Usage:
    python uart_receiver.py                    # recording mode, save to output.wav
    python uart_receiver.py --test             # test mode, serve sample-15s.wav
    python uart_receiver.py --test --hex-wav --sample soc-wuxi-arm/hw/sample_hex.txt -o received_sample_hex.wav
    python uart_receiver.py --port COM14       # use a different port
    python uart_receiver.py --baud 115200      # use a different baud rate
"""

import argparse
import os
import struct
import sys
import time
import wave

try:
    import serial
except ImportError:
    print("pyserial is required:  pip install pyserial")
    sys.exit(1)


# ---------------------------------------------------------------------------
# Protocol constants (keep in sync with main.c)
# ---------------------------------------------------------------------------
TAG_WAV0 = b"WAV0"           # recording-mode frame header
TAG_VFY0 = b"VFY0"           # recording-mode flash-header probe
TAG_REC0 = b"REC0"           # recording-mode recording-start marker
TAG_FMAX = b"FMAX"           # recording-mode FIFO max-usedw diagnostic
TAG_ACRC = b"ACRC"           # recording-mode audio Flash CRC result
TAG_RERR = b"RERR"           # recording-mode firmware error diagnostic
TAG_PVFY = b"PVFY"           # recording-mode page write/readback verify
TAG_PCRC = b"PCRC"           # recording-mode page CRC compare
TAG_BEND = b"BEND"           # recording-mode ADPCM block finished marker
TAG_ADBG = b"ADBG"           # ADPCM-only debug result
TAG_BCRC = b"BCRC"           # ADPCM RAM block -> Flash CRC result
TAG_WVT0 = b"WVT0"           # test-mode start
TAG_WVRQ = b"WVRQ"           # test-mode chunk request
TAG_WVRS = b"WVRS"           # test-mode result

FRAME_HEADER_SIZE = 4        # tag
FRAME_LEN_SIZE    = 4        # uint32 LE
RECV_CHUNK        = 4096     # bytes per serial read


# ---------------------------------------------------------------------------
# Low-level helpers
# ---------------------------------------------------------------------------
def read_exact(ser: serial.Serial, n: int) -> bytes:
    """Read exactly *n* bytes from the serial port (blocking)."""
    buf = b""
    while len(buf) < n:
        chunk = ser.read(n - len(buf))
        if not chunk:
            # Timeout – the FPGA won't send idle bytes, so this is an error.
            raise ConnectionError(
                f"Serial timeout after {len(buf)} of {n} bytes"
            )
        buf += chunk
    return buf


def recv_u32_le(ser: serial.Serial) -> int:
    """Read a little-endian uint32 from the serial port."""
    return struct.unpack("<I", read_exact(ser, 4))[0]


class SerialTextLogger:
    def __init__(self, ser: serial.Serial, log_path: str):
        self._ser = ser
        self._log = open(log_path, "w", encoding="utf-8", newline="")
        self._log.write(f"# UART RX log started {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        self._log.write("# Printable ASCII is written directly; other bytes use <HH>.\n\n")
        self._log.flush()

    def __getattr__(self, name):
        return getattr(self._ser, name)

    def _log_rx(self, data: bytes) -> None:
        if not data:
            return
        out = []
        for value in data:
            if value in (0x0A, 0x0D, 0x09):
                out.append(chr(value))
            elif 0x20 <= value <= 0x7E:
                out.append(chr(value))
            else:
                out.append(f"<{value:02X}>")
        self._log.write("".join(out))
        self._log.flush()

    def read(self, size: int = 1) -> bytes:
        data = self._ser.read(size)
        self._log_rx(data)
        return data

    def close(self) -> None:
        try:
            self._log.write(f"\n# UART RX log ended {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
            self._log.close()
        finally:
            self._ser.close()


# ---------------------------------------------------------------------------
# Recording mode: receive ACRC result or WAV0 + len + data
# ---------------------------------------------------------------------------
def run_recording(ser: serial.Serial, output_path: str) -> None:
    print("Waiting for recording-mode result (ACRC or WAV0)...")
    print("Press the board reset button, or the FPGA should start sending.")
    print()

    # --- wait for "ACRC" or "WAV0" tag ---
    tag_buf = b""
    while True:
        tag_buf += ser.read(1)
        tag_buf = tag_buf[-FRAME_HEADER_SIZE:]   # keep only last 4
        if tag_buf == TAG_REC0:
            print()
            print("REC0 received: recording has started now. Speak now.")
            print()
            tag_buf = b""
            continue
        if tag_buf == TAG_VFY0:
            status = recv_u32_le(ser)
            probe = read_exact(ser, 12)
            print("VFY0 received before WAV0:")
            print(f"  flash header status: 0x{status:08X}")
            print(f"  first 12 flash bytes: {probe.hex(' ')}")
            tag_buf = b""
            continue
        if tag_buf == TAG_FMAX:
            fifo_max = recv_u32_le(ser)
            print()
            print(f"FMAX received: ADPCM FIFO max usedw = {fifo_max} / 1023 (post-pop)")
            if fifo_max >= 1000:
                print("  *** WARNING: FIFO near full (>=1000) — overflow imminent! ***")
            elif fifo_max >= 900:
                print("  ** CAUTION: FIFO level high, marginal. **")
            elif fifo_max >= 500:
                print("  FIFO level moderate.")
            else:
                print("  FIFO level low — overflow unlikely.")
            print()
            tag_buf = b""
            continue
        if tag_buf == TAG_BEND:
            block_index = recv_u32_le(ser)
            print(f"BEND received: ADPCM block {block_index} finished.")
            tag_buf = b""
            continue
        if tag_buf == TAG_ADBG:
            status = recv_u32_le(ser)
            word_count = recv_u32_le(ser)
            crc = recv_u32_le(ser)
            fifo_max = recv_u32_le(ser)
            error_word = recv_u32_le(ser)
            print()
            print("ADBG received: ADPCM-only debug result")
            print(f"  status: 0x{status:08X}")
            print(f"  words read: {word_count}")
            print(f"  ADPCM data CRC: 0x{crc:08X}")
            print(f"  ADPCM FIFO max usedw: {fifo_max} / 1023 (post-pop)")
            print(f"  error word index: {error_word}")
            tag_buf = b""
            continue
        if tag_buf == TAG_BCRC:
            status = recv_u32_le(ser)
            block_bytes = recv_u32_le(ser)
            source_crc = recv_u32_le(ser)
            flash_crc = recv_u32_le(ser)
            fifo_max = recv_u32_le(ser)
            erase_bad_count = recv_u32_le(ser)
            first_mismatch_offset = recv_u32_le(ser)
            first_expected = recv_u32_le(ser)
            first_actual = recv_u32_le(ser)
            first_actual2 = recv_u32_le(ser)
            first_wren_sr = recv_u32_le(ser)
            first_pp_sr = recv_u32_le(ser)
            first_wait_sr = recv_u32_le(ser)
            jedec_raw = [recv_u32_le(ser) & 0xFF for _ in range(8)]
            sr_before_clear = recv_u32_le(ser)
            sr_after_clear = recv_u32_le(ser)
            print()
            print("BCRC received: RAM-captured ADPCM block Flash CRC result")
            print(f"  status: 0x{status:08X}")
            print(f"  block bytes: {block_bytes}")
            print(f"  source CRC: 0x{source_crc:08X}")
            print(f"  flash CRC:  0x{flash_crc:08X}")
            print(f"  ADPCM FIFO max usedw: {fifo_max} / 1023 (post-pop)")
            print(f"  erase non-FF bytes in first block: {erase_bad_count}")
            if first_mismatch_offset != 0xFFFFFFFF:
                print(f"  first mismatch offset: {first_mismatch_offset} (0x{first_mismatch_offset:08X})")
                print(f"  expected byte: 0x{first_expected & 0xFF:02X}")
                print(f"  actual byte:   0x{first_actual & 0xFF:02X}")
                print(f"  second stable read actual byte: 0x{first_actual2 & 0xFF:02X}")
            print(f"  first WREN SR:       0x{first_wren_sr & 0xFF:02X}")
            print(f"  first PP immediate SR: 0x{first_pp_sr & 0xFF:02X}")
            print(f"  first PP wait SR:      0x{first_wait_sr & 0xFF:02X}")
            print("  JEDEC raw before erase/program:", " ".join(f"{b:02X}" for b in jedec_raw))
            print(f"  SR before clear BP:    0x{sr_before_clear & 0xFF:02X}")
            print(f"  SR after clear BP:     0x{sr_after_clear & 0xFF:02X}")
            if source_crc == flash_crc:
                print("  PASS: Flash readback matches the RAM-captured ADPCM block.")
            else:
                print("  FAIL: Flash readback differs from the RAM-captured ADPCM block.")
            return
        if tag_buf == TAG_RERR:
            error_flags = recv_u32_le(ser)
            block_index = recv_u32_le(ser)
            word_index = recv_u32_le(ser)
            print()
            print("RERR received: firmware hit a recording timeout")
            print(f"  error flags: 0x{error_flags:08X}")
            print(f"  block index: {block_index}")
            print(f"  word index: {word_index}")
            if error_flags & 0x00000001:
                print("  AUDIO_TIMEOUT: ADPCM FIFO stopped producing words.")
            if error_flags & 0x00000002:
                print("  FLASH_TIMEOUT: SPI Flash busy polling exceeded the limit.")
            if error_flags & 0x00000004:
                print("  PAGE_VERIFY: at least one Flash page readback mismatched its source buffer.")
            print()
            tag_buf = b""
            continue
        if tag_buf == TAG_PVFY:
            fail_count = recv_u32_le(ser)
            first_offset = recv_u32_le(ser)
            first_expected = recv_u32_le(ser)
            first_actual = recv_u32_le(ser)
            first_missing_zero = recv_u32_le(ser)
            first_unwanted_zero = recv_u32_le(ser)
            read_unstable_count = recv_u32_le(ser)
            first_actual2 = recv_u32_le(ser)
            print()
            print("PVFY received: per-page Flash write/readback check")
            print(f"  failed pages: {fail_count}")
            print(f"  bytes where first/second read differed: {read_unstable_count}")
            if fail_count or read_unstable_count:
                print(f"  first mismatch offset: {first_offset} (0x{first_offset:08X})")
                print(f"  expected byte: 0x{first_expected & 0xFF:02X}")
                print(f"  actual byte:   0x{first_actual & 0xFF:02X}")
                print(f"  second read actual byte: 0x{first_actual2 & 0xFF:02X}")
                print(f"  bits still 1 but expected 0: 0x{first_missing_zero & 0xFF:02X}")
                print(f"  bits became 0 but expected 1: 0x{first_unwanted_zero & 0xFF:02X}")
            else:
                print("  all programmed pages matched immediately after write.")
            print()
            tag_buf = b""
            continue
        if tag_buf == TAG_PCRC:
            page_count = recv_u32_le(ser)
            fail_count = recv_u32_le(ser)
            first_page = recv_u32_le(ser)
            first_offset = recv_u32_le(ser)
            first_len = recv_u32_le(ser)
            source_crc = recv_u32_le(ser)
            flash_crc = recv_u32_le(ser)
            print()
            print("PCRC received: per-page source-vs-Flash CRC compare")
            print(f"  pages written: {page_count}")
            print(f"  failed pages: {fail_count}")
            if fail_count:
                print(f"  first bad page: {first_page}")
                print(f"  first bad offset: {first_offset} (0x{first_offset:08X})")
                print(f"  first bad length: {first_len}")
                print(f"  source page CRC: 0x{source_crc:08X}")
                print(f"  flash page CRC:  0x{flash_crc:08X}")
            else:
                print("  all page CRCs matched.")
            print()
            tag_buf = b""
            continue
        if tag_buf == TAG_ACRC:
            status = recv_u32_le(ser)
            wav_bytes = recv_u32_le(ser)
            source_wav_crc = recv_u32_le(ser)
            flash_wav_crc = recv_u32_le(ser)
            source_adpcm_crc = recv_u32_le(ser)
            flash_adpcm_crc = recv_u32_le(ser)
            fifo_max = recv_u32_le(ser)
            wav_ok = source_wav_crc == flash_wav_crc
            adpcm_ok = source_adpcm_crc == flash_adpcm_crc

            print()
            print("ACRC received: audio Flash CRC check result")
            print(f"  status: 0x{status:08X}")
            print(f"  wav bytes: {wav_bytes}")
            print(f"  WAV stream CRC: source=0x{source_wav_crc:08X} flash=0x{flash_wav_crc:08X} {'OK' if wav_ok else 'MISMATCH'}")
            print(f"  ADPCM data CRC: source=0x{source_adpcm_crc:08X} flash=0x{flash_adpcm_crc:08X} {'OK' if adpcm_ok else 'MISMATCH'}")
            print(f"  ADPCM FIFO max usedw: {fifo_max} / 1023 (post-pop)")
            if wav_ok and adpcm_ok:
                print("  PASS: Flash readback matches generated WAV/ADPCM bytes.")
            else:
                print("  FAIL: Flash readback differs from generated WAV/ADPCM bytes.")

            print()
            print("Waiting for WAV data (WAV0 frame)...")
            # Keep waiting for the WAV0 tag that follows ACRC.
            tag_buf = b""
            while True:
                tag_buf += ser.read(1)
                tag_buf = tag_buf[-FRAME_HEADER_SIZE:]
                if tag_buf == TAG_WAV0:
                    break
            break
        if tag_buf == TAG_WAV0:
            break

    # --- read 4-byte file length (little-endian) ---
    file_len = recv_u32_le(ser)
    print(f"WAV0 received.  File length: {file_len} bytes  "
          f"({file_len / 1024:.1f} KiB)")

    remaining = file_len
    received = 0

    with open(output_path, "wb") as f:
        while remaining > 0:
            chunk = ser.read(min(RECV_CHUNK, remaining))
            if not chunk:
                print(f"\nError: timeout after {received}/{file_len} bytes")
                break
            f.write(chunk)
            received += len(chunk)
            remaining -= len(chunk)

            # progress
            pct = received * 100.0 / file_len if file_len else 100.0
            print(f"\rReceiving... {received}/{file_len}  ({pct:.1f}%)", end="")

    print()
    print(f"Done.  Wrote {received} bytes to {output_path}")
    validate_wav_file(output_path, file_len)


# ---------------------------------------------------------------------------
# Test mode: serve a pre-existing WAV file chunk-by-chunk to the FPGA
# ---------------------------------------------------------------------------
def load_sample_payload(sample_path: str, hex_wav: bool) -> bytes:
    if not hex_wav:
        with open(sample_path, "rb") as f:
            return f.read()

    payload = bytearray()
    with open(sample_path, "r", encoding="ascii") as f:
        for lineno, line in enumerate(f, 1):
            word = line.strip()
            if not word:
                continue
            if len(word) != 4:
                raise ValueError(f"{sample_path}:{lineno}: expected 4 hex digits")
            payload += int(word, 16).to_bytes(2, "little", signed=False)
    return bytes(payload)


def write_pcm_wav(output_path: str, pcm_bytes: bytes, sample_rate: int) -> None:
    with wave.open(output_path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm_bytes)


def validate_wav_file(output_path: str, expected_len=None) -> None:
    with open(output_path, "rb") as f:
        data = f.read(128)

    actual_len = os.path.getsize(output_path)
    if expected_len is not None and actual_len != expected_len:
        print(f"  ERROR: saved length {actual_len} != frame length {expected_len}")
        return

    if len(data) < 12 or data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        riff_pos = data.find(b"RIFF")
        print("  ERROR: saved file is not a RIFF/WAVE file at byte 0.")
        if riff_pos > 0:
            print(f"  HINT: RIFF appears at byte {riff_pos}; extra bytes preceded the WAV.")
        elif data:
            print(f"  first 16 bytes: {data[:16].hex(' ')}")
        return

    riff_size = struct.unpack_from("<I", data, 4)[0]
    pos = 12
    fmt = None
    fact_samples = None
    data_size = None

    while pos + 8 <= len(data):
        chunk_id = data[pos:pos + 4]
        chunk_size = struct.unpack_from("<I", data, pos + 4)[0]
        chunk_data = pos + 8

        if chunk_id == b"fmt " and chunk_data + min(chunk_size, 20) <= len(data):
            audio_format, channels, sample_rate, byte_rate, block_align, bits = struct.unpack_from(
                "<HHIIHH", data, chunk_data
            )
            extra_size = None
            samples_per_block = None
            if chunk_size >= 20 and chunk_data + 20 <= len(data):
                extra_size = struct.unpack_from("<H", data, chunk_data + 16)[0]
                samples_per_block = struct.unpack_from("<H", data, chunk_data + 18)[0]
            fmt = (audio_format, channels, sample_rate, byte_rate,
                   block_align, bits, extra_size, samples_per_block)
        elif chunk_id == b"fact" and chunk_data + 4 <= len(data):
            fact_samples = struct.unpack_from("<I", data, chunk_data)[0]
        elif chunk_id == b"data":
            data_size = chunk_size
            break

        pos = chunk_data + chunk_size + (chunk_size & 1)

    print("  WAV header check:")
    print(f"    RIFF size field: {riff_size}  file bytes: {actual_len}")
    if riff_size + 8 != actual_len:
        print("    WARNING: RIFF size does not match saved file length.")

    if fmt is None:
        print("    ERROR: fmt chunk not found in first 128 bytes.")
        return

    audio_format, channels, sample_rate, byte_rate, block_align, bits, extra_size, samples_per_block = fmt
    print(f"    format=0x{audio_format:04X} channels={channels} rate={sample_rate} "
          f"bits={bits} block_align={block_align}")
    if audio_format == 0x0011:
        print(f"    IMA ADPCM extra={extra_size} samples_per_block={samples_per_block} "
              f"fact_samples={fact_samples} data_size={data_size}")
        if block_align != 4096 or samples_per_block != 8185:
            print("    WARNING: ADPCM block parameters differ from firmware constants.")
        else:
            print("    OK: looks like a valid IMA ADPCM WAV header.")
    elif audio_format == 0x0001:
        print("    OK: looks like a PCM WAV header.")
    else:
        print("    WARNING: uncommon WAV format; some players may not support it.")


def discard_possible_echo(ser: serial.Serial, data: bytes) -> None:
    """Drop bytes that immediately echo the PC-to-board payload."""
    pos = 0
    deadline = time.monotonic() + 0.05
    while pos < len(data) and time.monotonic() < deadline:
        waiting = ser.in_waiting
        if waiting <= 0:
            time.sleep(0.001)
            continue
        chunk = ser.read(min(waiting, len(data) - pos))
        if chunk != data[pos:pos + len(chunk)]:
            print("\n  warning: serial echo did not match payload prefix")
            return
        pos += len(chunk)
    if pos:
        print(f"\n  discarded {pos} echoed byte(s)")


def run_test_mode(ser: serial.Serial, sample_path: str, output_path: str,
                  hex_wav: bool, sample_rate: int, discard_echo: bool) -> None:
    sample_bytes = load_sample_payload(sample_path, hex_wav)
    if hex_wav:
        print(f"Loaded {sample_path} as 16-bit mono PCM  "
              f"({len(sample_bytes)} bytes, {len(sample_bytes)//2} samples)")
    else:
        print(f"Loaded {sample_path}  ({len(sample_bytes)} bytes)")

    # --- wait for "WVT0" start message ---
    print("Waiting for test-mode start (WVT0)...")
    tag_buf = b""
    last_wait_report = time.monotonic()
    while True:
        b = ser.read(1)
        if not b:
            now = time.monotonic()
            if now - last_wait_report >= 5.0:
                print("  still waiting for WVT0...")
                last_wait_report = now
            continue
        tag_buf += b
        tag_buf = tag_buf[-FRAME_HEADER_SIZE:]
        if tag_buf == TAG_WVT0:
            break

    sample_len  = recv_u32_le(ser)
    flash_addr  = recv_u32_le(ser)
    page_bytes  = recv_u32_le(ser)
    print(f"WVT0 received.  len={sample_len}  flash_addr=0x{flash_addr:08X}  "
          f"page_bytes={page_bytes}")

    if sample_len != len(sample_bytes):
        print(f"WARNING: FPGA expects {sample_len} bytes but sample file has "
              f"{len(sample_bytes)} bytes — sizes should match.")

    # --- chunk-request loop ---
    chunks_served = 0
    expected_offset = 0
    last_wait_report = time.monotonic()
    bad_frame_reports = 0
    while True:
        tag_buf = b""
        while True:
            b = ser.read(1)
            if not b:
                now = time.monotonic()
                if now - last_wait_report >= 5.0:
                    print(f"\n  waiting for WVRQ/WVRS... chunks={chunks_served} "
                          f"expected_offset={expected_offset}")
                    if expected_offset > 0 and chunks_served <= 1:
                        print("  HINT: If stuck here, FPGA may be getting UART RX noise "
                              "from SPI activity. Try re-running with updated firmware.")
                    last_wait_report = now
                continue
            tag_buf += b
            tag_buf = tag_buf[-FRAME_HEADER_SIZE:]
            if tag_buf == TAG_WVRQ or tag_buf == TAG_WVRS:
                break

        if tag_buf == TAG_WVRS:
            # Test finished — read the result.
            status     = recv_u32_le(ser)
            result_len = recv_u32_le(ser)
            crc_src    = recv_u32_le(ser)
            crc_flash  = recv_u32_le(ser)

            status_names = {
                0x574F4B21: "WOK! (PASS)",
                0x5746414C: "WFAL (FAIL)",
                0x57424947: "WBIG (too big)",
            }
            if status not in status_names:
                print(f"\n  WARNING: Unknown WVRS status=0x{status:08X}, ignoring...")
                continue
            if status == 0x574F4B21 and result_len != sample_len:
                print(f"\n  WARNING: WVRS result_len={result_len} != sample_len={sample_len}, ignoring...")
                continue
            if (status == 0x5746414C and
                    (result_len > sample_len or (result_len % page_bytes) != 0)):
                print(f"\n  WARNING: WVRS invalid fail_offset={result_len}, ignoring...")
                continue

            status_str = status_names.get(status,
                                          f"UNKNOWN (0x{status:08X})")
            print()
            print(f"WVRS received — test complete.")
            print(f"  status:     0x{status:08X}  {status_str}")
            if status == 0x574F4B21:
                print(f"  wav_bytes:  {result_len}")
            else:
                print(f"  fail_offset/result: {result_len}")
            print(f"  source_crc: 0x{crc_src:08X}")
            print(f"  flash_crc:  0x{crc_flash:08X}")
            if crc_src == crc_flash and status == 0x574F4B21:
                print("  PASS: Flash content matches source!")
            else:
                print("  MISMATCH: Flash content differs from source.")

            if status == 0x574F4B21:
                print()
                print("Waiting for flash readback frame (WAV0)...")
                tag_buf = b""
                while True:
                    tag_buf += ser.read(1)
                    tag_buf = tag_buf[-FRAME_HEADER_SIZE:]
                    if tag_buf == TAG_WAV0:
                        break

                readback_len = recv_u32_le(ser)
                print(f"WAV0 received.  Readback length: {readback_len} bytes")

                remaining = readback_len
                received = 0
                readback = bytearray()
                raw_output_path = output_path if not hex_wav else output_path + ".pcm.tmp"
                with open(raw_output_path, "wb") as f:
                    while remaining > 0:
                        chunk_data = ser.read(min(RECV_CHUNK, remaining))
                        if not chunk_data:
                            raise ConnectionError(
                                f"Serial timeout after {received}/{readback_len} readback bytes"
                            )
                        f.write(chunk_data)
                        readback += chunk_data
                        received += len(chunk_data)
                        remaining -= len(chunk_data)

                        pct = received * 100.0 / readback_len if readback_len else 100.0
                        print(f"\rReceiving readback... {received}/{readback_len}  ({pct:.1f}%)", end="")

                print()
                if hex_wav:
                    write_pcm_wav(output_path, bytes(readback), sample_rate)
                    try:
                        os.remove(raw_output_path)
                    except OSError:
                        pass
                    print(f"Done.  Wrote flash readback WAV to {output_path}")
                else:
                    print(f"Done.  Wrote flash readback to {output_path}")

                if readback_len == len(sample_bytes):
                    if bytes(readback) == sample_bytes:
                        print("  PASS: Readback file is byte-identical to source.")
                    else:
                        print("  MISMATCH: Readback file differs from source.")
                else:
                    print("  WARNING: Readback length differs from source length.")
            break

        elif tag_buf == TAG_WVRQ:
            offset = recv_u32_le(ser)
            chunk  = recv_u32_le(ser)

            expected_chunk = min(page_bytes, sample_len - offset)
            if (chunk == 0 or
                    chunk != expected_chunk or
                    offset + chunk > sample_len or
                    (offset % page_bytes) != 0):
                if bad_frame_reports < 16:
                    print(f"\n  ignored WVRQ: offset={offset} chunk={chunk} "
                          f"expected_offset={expected_offset} expected_chunk={expected_chunk}")
                    bad_frame_reports += 1
                continue

            if offset != expected_offset:
                block_retry = (
                    offset < expected_offset and
                    (offset % 65536) == 0
                )
                if not block_retry:
                    if bad_frame_reports < 16:
                        print(f"\n  ignored WVRQ order: offset={offset} "
                              f"expected_offset={expected_offset} chunk={chunk}")
                        bad_frame_reports += 1
                    continue

                print(f"\n  block retry detected: offset={offset}")
                expected_offset = offset

            if offset != expected_offset:
                continue

            chunks_served += 1

            # Validate range.
            if offset + chunk > len(sample_bytes):
                print(f"\nERROR: WVRQ offset {offset} + chunk {chunk} "
                      f"exceeds file length {len(sample_bytes)}")
                # Send zero-padded response to let the FPGA continue.
                data = b"\x00" * chunk
            else:
                data = sample_bytes[offset : offset + chunk]

            ser.write(data)
            ser.flush()
            if discard_echo:
                # Only use when USB-UART bridge echo is confirmed.
                # Adds ~50ms per chunk — too slow for large files.
                discard_possible_echo(ser, data)
            expected_offset = offset + chunk

            print(f"\rServed chunk {chunks_served}: "
                  f"offset={offset} len={chunk}", end="")

    print()
    print(f"Test-mode finished.  {chunks_served} chunk(s) served.")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="PC-side UART receiver for SoC audio recorder"
    )
    parser.add_argument("--port",  default="COM14",
                        help="Serial port name (default: COM14)")
    parser.add_argument("--baud",  type=int, default=115200,
                        help="Baud rate (default: 115200)")
    parser.add_argument("--test",  action="store_true",
                        help="Run in test mode (serve a sample file to FPGA)")
    parser.add_argument("--hex-wav", action="store_true",
                        help="Treat --sample as 4-hex-digit-per-line mono 16-bit PCM and save readback as WAV")
    parser.add_argument("--sample-rate", type=int, default=15625,
                        help="Sample rate for --hex-wav output WAV (default: 15625)")
    parser.add_argument("--discard-echo", action="store_true",
                        help="Discard immediate serial echo of PC-to-board chunks")
    parser.add_argument("--serial-log",
                        help="Save UART RX bytes to a readable text log file")
    parser.add_argument("--sample",
                        default="sample-15s.wav",
                        help="Sample file for test mode "
                             "(default: sample-15s.wav)")
    parser.add_argument("--output", "-o",
                        default="received.wav",
                        help="Output WAV file for recording mode "
                             "(default: received.wav)")
    args = parser.parse_args()

    print(f"Opening {args.port} at {args.baud} baud, 8N1 ...")
    ser = serial.Serial(
        port     = args.port,
        baudrate = args.baud,
        bytesize = serial.EIGHTBITS,
        parity   = serial.PARITY_NONE,
        stopbits = serial.STOPBITS_ONE,
        timeout  = 0.5,     # 500 ms per read — adapt if needed
    )
    if args.serial_log:
        ser = SerialTextLogger(ser, args.serial_log)
        print(f"UART RX text log: {args.serial_log}")

    # Flush any stale bytes.
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    try:
        if args.test:
            run_test_mode(ser, args.sample, args.output,
                          args.hex_wav, args.sample_rate, args.discard_echo)
        else:
            run_recording(ser, args.output)
    except KeyboardInterrupt:
        print("\nInterrupted by user.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()

import math
import random
import threading
import time
import tkinter as tk
from enum import IntEnum
from tkinter import ttk

import serial

# --- Wire protocol ---------------------------------------------------------
# Mirrors firmware/components/protocol/include/protocol.hpp

PROTOCOL_START_BYTE = 0xA5
MIXER_CHANNELS = 4
CHANNEL_NAME_LEN = 16


class PacketType(IntEnum):
    LEVELS = 0x01        # host -> device
    METADATA = 0x02      # host -> device
    COMMAND_EVENT = 0x03 # device -> host


# Mirrors Command in protocol.hpp exactly -- these are wire values, not
# reassignable without breaking whatever firmware build is on the device.
class Command(IntEnum):
    SWITCH_1 = 0x00
    SWITCH_2 = 0x01
    SWITCH_3 = 0x02
    SWITCH_4 = 0x03
    SWITCH_5 = 0x04
    SWITCH_6 = 0x05
    SWITCH_7 = 0x06
    SWITCH_8 = 0x07

    ENCODER_1_PLUS = 0x10
    ENCODER_1_MINUS = 0x11
    ENCODER_1_BTN = 0x12
    ENCODER_2_PLUS = 0x13
    ENCODER_2_MINUS = 0x14
    ENCODER_2_BTN = 0x15
    ENCODER_3_PLUS = 0x16
    ENCODER_3_MINUS = 0x17
    ENCODER_3_BTN = 0x18
    ENCODER_4_PLUS = 0x19
    ENCODER_4_MINUS = 0x1A
    ENCODER_4_BTN = 0x1B


class Level(IntEnum):
    MIN = 0
    MAX = 100


class MeterRange:
    DB_FLOOR = -60.0  # dBFS treated as silence
    DB_CEILING = 0.0  # dBFS treated as full scale


def amplitude_to_level(peak_amplitude: float) -> int:
    """Convert a linear 0.0-1.0 peak amplitude (as returned by WASAPI's
    IAudioMeterInformation::GetChannelsPeakValues, or computed by hand from
    a PipeWire monitor stream) into a 0-100 level on a dBFS scale, since
    audio energy -- and how loud it looks on a meter -- is logarithmic."""
    peak_amplitude = max(peak_amplitude, 1e-6)  # avoid log10(0)
    db = 20 * math.log10(peak_amplitude)
    db = max(MeterRange.DB_FLOOR, min(MeterRange.DB_CEILING, db))
    normalized = (db - MeterRange.DB_FLOOR) / (MeterRange.DB_CEILING - MeterRange.DB_FLOOR)
    return round(normalized * (Level.MAX - Level.MIN)) + Level.MIN


def clamp_level(value: int) -> int:
    return max(Level.MIN, min(Level.MAX, value))


def _checksum(data: bytes) -> int:
    value = 0
    for b in data:
        value ^= b
    return value


def _frame(body: bytes) -> bytes:
    """Wraps a packet body (type byte + payload) with the sync byte and
    trailing checksum every packet type shares."""
    return bytes([PROTOCOL_START_BYTE]) + body + bytes([_checksum(body)])


def build_levels_packet(volumes, lefts, rights, valid: bool) -> bytes:
    """volume/left/right per channel, 0-100. Sent at high frequency."""
    body = bytes([PacketType.LEVELS])
    for i in range(MIXER_CHANNELS):
        body += bytes([volumes[i], lefts[i], rights[i]])
    body += bytes([1 if valid else 0])

    return _frame(body)


def build_metadata_packet(names) -> bytes:
    """Channel names. Sent once on connect, or whenever a name changes."""
    body = bytes([PacketType.METADATA])
    for name in names:
        # Truncate to leave room for a guaranteed null terminator -- the
        # firmware bounds-checks this field, but don't rely on that.
        encoded = name.encode("ascii")[: CHANNEL_NAME_LEN - 1]
        body += encoded.ljust(CHANNEL_NAME_LEN, b"\x00")

    return _frame(body)


# --- Serial I/O --------------------------------------------------------------

SERIAL_PORT = "/dev/cu.usbmodemVM0011"
BAUD_RATE = 115200

CHANNEL_NAMES = ["GAME", "CHAT", "MUSIC", "SYSTEM"]
STARTING_VOLUME = 50


def send_levels_packet(ser, volumes, lefts, rights, valid=True):
    packet = build_levels_packet(volumes, lefts, rights, valid)
    ser.write(packet)
    ser.flush()


def send_metadata_packet(ser, names):
    packet = build_metadata_packet(names)
    ser.write(packet)
    ser.flush()
    print(f"[TX metadata] {names}")


def read_command_event(ser):
    """Reads one COMMAND_EVENT off the wire, blocking up to ser.timeout per
    read() call. Returns the Command, or None if nothing usable arrived
    (timeout, bad sync byte, unknown type, bad checksum, unknown command)."""
    header = ser.read(1)
    if len(header) != 1 or header[0] != PROTOCOL_START_BYTE:
        return None

    type_byte = ser.read(1)
    if len(type_byte) != 1 or type_byte[0] != PacketType.COMMAND_EVENT:
        return None

    body = ser.read(2)  # command, checksum
    if len(body) != 2:
        return None

    command_byte, checksum = body
    if _checksum(bytes([type_byte[0], command_byte])) != checksum:
        print("[RX command] discarded: checksum mismatch")
        return None

    try:
        return Command(command_byte)
    except ValueError:
        print(f"[RX command] discarded: unknown command 0x{command_byte:02X}")
        return None


# Encoder N's PLUS/MINUS -> (channel index, volume delta). Encoder buttons
# and switches aren't mapped to anything yet; they're just logged.
ENCODER_VOLUME_DELTA = {
    Command.ENCODER_1_PLUS: (0, 1),
    Command.ENCODER_1_MINUS: (0, -1),
    Command.ENCODER_2_PLUS: (1, 1),
    Command.ENCODER_2_MINUS: (1, -1),
    Command.ENCODER_3_PLUS: (2, 1),
    Command.ENCODER_3_MINUS: (2, -1),
    Command.ENCODER_4_PLUS: (3, 1),
    Command.ENCODER_4_MINUS: (3, -1),
}


def command_reader_loop(ser, volumes, volumes_lock):
    while True:
        command = read_command_event(ser)
        if command is None:
            continue

        delta = ENCODER_VOLUME_DELTA.get(command)
        if delta is None:
            print(f"[RX command] {command.name}")
            continue

        channel, step = delta
        with volumes_lock:
            volumes[channel] = clamp_level(volumes[channel] + step)


# --- Synthetic peak meter data, for testing without a real audio source ------
# Volume is real interactive state now (driven by the encoders); left/right
# peaks are still simulated since there's no audio source hooked up. Real
# audio energy is bursty/irregular (transients, silence gaps), not a smooth
# tone, so this models it as an envelope that jumps up on random "hits" and
# decays between them, rather than a pure cosine.

HIT_CHANCE = 0.15  # per-tick chance of a new transient on a given channel
DECAY_MIN = 0.75
DECAY_MAX = 0.95

_channel_envelope = [0.0] * MIXER_CHANNELS


def generate_synthetic_peaks(volumes):
    """Per-channel left/right peak levels (0-100): simulates raw linear
    amplitude (0.0-1.0) as read from a peak-meter API, scaled by the
    channel's volume (a linear 0-1 gain, same as a real fader/slider would
    apply before metering) and then run through amplitude_to_level() -- the
    same path real WASAPI/PipeWire capture would take."""
    lefts, rights = [], []
    for ch in range(MIXER_CHANNELS):
        if random.random() < HIT_CHANCE:
            _channel_envelope[ch] = random.uniform(0.4, 1.0)
        else:
            _channel_envelope[ch] *= random.uniform(DECAY_MIN, DECAY_MAX)

        left_amp = min(1.0, _channel_envelope[ch] + random.uniform(0.0, 0.05))
        right_amp = min(1.0, _channel_envelope[ch] + random.uniform(0.0, 0.05))

        gain = volumes[ch] / Level.MAX
        lefts.append(amplitude_to_level(left_amp * gain))
        rights.append(amplitude_to_level(right_amp * gain))
    return lefts, rights


def sender_loop(ser, volumes, volumes_lock):
    while True:
        with volumes_lock:
            volumes_snapshot = list(volumes)

        lefts, rights = generate_synthetic_peaks(volumes_snapshot)
        send_levels_packet(ser, volumes_snapshot, lefts, rights, valid=True)

        time.sleep(0.016)  # ~60Hz


# --- GUI ----------------------------------------------------------------------


def run_gui(volumes, volumes_lock):
    root = tk.Tk()
    root.title("Velvet Mixer Client")

    bars = []
    labels = []

    for i in range(MIXER_CHANNELS):
        frame = ttk.Frame(root, padding=10)
        frame.grid(row=0, column=i, sticky="n")

        ttk.Label(frame, text=CHANNEL_NAMES[i]).pack()

        bar = ttk.Progressbar(
            frame, orient="vertical", length=200, mode="determinate", maximum=Level.MAX
        )
        bar.pack(pady=8)
        bars.append(bar)

        label = ttk.Label(frame, text=f"{STARTING_VOLUME}%")
        label.pack()
        labels.append(label)

    def refresh():
        with volumes_lock:
            volumes_snapshot = list(volumes)

        for i, value in enumerate(volumes_snapshot):
            bars[i]["value"] = value
            labels[i]["text"] = f"{value}%"

        root.after(50, refresh)

    refresh()
    root.mainloop()


def main():
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print("Waiting for ESP32 reboot...")
        time.sleep(2)

        print("Connected! Sending channel names...")
        send_metadata_packet(ser, CHANNEL_NAMES)

        volumes = [STARTING_VOLUME] * MIXER_CHANNELS
        volumes_lock = threading.Lock()

        reader_thread = threading.Thread(
            target=command_reader_loop, args=(ser, volumes, volumes_lock), daemon=True
        )
        reader_thread.start()

        sender_thread = threading.Thread(
            target=sender_loop, args=(ser, volumes, volumes_lock), daemon=True
        )
        sender_thread.start()

        run_gui(volumes, volumes_lock)

    except serial.SerialException as e:
        print(f"\nSerial Error: {e}")
    except KeyboardInterrupt:
        print("\nScript terminated by user. Goodbye!")


if __name__ == "__main__":
    main()

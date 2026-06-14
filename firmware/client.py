import serial
import time
import struct
import random

import math

BASE      = 50     # media attorno a cui oscilla
AMPLITUDE = 40     # quanto si allontana dalla media (qui 10..90)
FREQ      = 0.5    # Hz: oscillazioni al secondo (0.5 = un ciclo ogni 2s)

start_time = time.time()

GLOBAL_AMP = 30   # parte comune -> muove il master/RMS
LOCAL_AMP  = 15   # parte per-canale -> differenzia i canali

def generate_musical_channels():
    t = time.time() - start_time
    global_wave = math.cos(2 * math.pi * FREQ * t)              # uguale per tutti
    channels = []
    for ch in range(NUM_CHANNELS):
        phase = ch * (2 * math.pi / NUM_CHANNELS)
        local = math.cos(2 * math.pi * FREQ * 1.7 * t + phase)  # diverso per canale
        val = BASE + GLOBAL_AMP * global_wave + LOCAL_AMP * local
        channels.append(int(max(0, min(100, val))))
    return channels

# SERIAL CONFIGURATION
SERIAL_PORT = '/dev/ttyACM0' 
BAUD_RATE = 115200
START_BYTE = 0xA5
NUM_CHANNELS = 5

def send_packet(ser, volumes):
    header = START_BYTE
    valid = 1  # Wakes up the ESP32 screen
    
    # Calculate checksum including the 'valid' byte
    data_payload = bytes(volumes) + bytes([valid])
    checksum = 0
    for b in data_payload:
        checksum ^= b
        
    # Packet format: 1 (Header) + 5 (Volumes) + 1 (Valid) + 1 (Checksum) = 8B
    packet_format = "<BBBBBBBB"
    packet_bytes = struct.pack(packet_format, header, *volumes, valid, checksum)
    
    ser.write(packet_bytes)
    ser.flush()
    
    # Debug print of sent packets to console
    channels_str = ", ".join(f"{v:3d}" for v in volumes)
    print(f"[TX] Channels: [{channels_str}] | Checksum: 0x{checksum:02X}")

def generate_musical_channels():
    """
    Generates 5 channels from 0 to 100 with internal coherence
    to simulate the behavior of a real audio source.
    """
    # Choose a general intensity level for this specific frame
    vibe = random.choice(["low", "medium", "high"])
    
    if vibe == "low":
        return [random.randint(0, 30) for _ in range(NUM_CHANNELS)]
    elif vibe == "medium":
        return [random.randint(20, 65) for _ in range(NUM_CHANNELS)]
    else:
        return [random.randint(50, 100) for _ in range(NUM_CHANNELS)]

def main():
    try:
        # Initialize serial connection
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print("Waiting for ESP32 reboot...")
        time.sleep(2)
        print("Connected! Sending data at ~16ms (60Hz)...")
        
        while True:
            # Generate volumes (0-100) for the 5 channels
            volumes = generate_musical_channels()
            
            # Send the packet to the ESP32
            send_packet(ser, volumes)
            
            # 16ms delay corresponds to roughly 60 packets per second
            time.sleep(0.066)
            
    except serial.SerialException as e:
        print(f"\nSerial Error: {e}")
    except KeyboardInterrupt:
        print("\nScript terminated by user. Goodbye!")

if __name__ == "__main__":
    main()

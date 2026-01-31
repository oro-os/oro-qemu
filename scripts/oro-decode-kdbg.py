#!/usr/bin/env python3
"""
Oro kernel debug packet decoder
Decodes packets from the oro_kdbg MMIO device
"""

import sys
import struct

def decode_reg0(reg0):
    """Decode the encoded reg[0] value"""
    is_qemu_event = (reg0 >> 63) & 1
    bitmask = (reg0 >> 56) & 0x7F
    cpu_index = (reg0 >> 48) & 0xFF
    command_id = reg0 & 0x0000FFFFFFFFFFFF
    
    return {
        'is_qemu_event': bool(is_qemu_event),
        'bitmask': bitmask,
        'cpu_index': cpu_index if cpu_index != 0xFF else None,
        'command_id': command_id
    }

def decode_packet(data):
    """Decode a packet from the oro_kdbg device"""
    if len(data) < 8:
        return None
    
    # First register is always present
    reg0 = struct.unpack('<Q', data[0:8])[0]
    info = decode_reg0(reg0)
    
    # Calculate expected packet size from bitmask
    num_additional_regs = bin(info['bitmask']).count('1')
    expected_size = 8 + (num_additional_regs * 8)
    
    # Check if we have enough data for the complete packet
    if len(data) < expected_size:
        return None  # Not enough data yet, wait for more
    
    # Decode additional registers based on bitmask
    registers = {}
    data_offset = 8
    
    for i in range(7):
        if info['bitmask'] & (1 << i):
            reg_value = struct.unpack('<Q', data[data_offset:data_offset+8])[0]
            registers[i + 1] = reg_value
            data_offset += 8
    
    return {
        'source': 'QEMU' if info['is_qemu_event'] else 'Kernel',
        'cpu_index': info['cpu_index'],
        'command_id': info['command_id'],
        'registers': registers,
        'packet_size': expected_size
    }

def main():
    if len(sys.argv) > 1:
        # Read from file (buffered, all at once)
        with open(sys.argv[1], 'rb') as f:
            data = f.read()
        
        offset = 0
        packet_num = 0
        
        while offset < len(data):
            remaining = data[offset:]
            if len(remaining) < 8:
                print(f"Warning: Trailing {len(remaining)} bytes at end of stream", file=sys.stderr)
                break
            
            packet = decode_packet(remaining)
            if not packet:
                break
            
            packet_num += 1
            print(f"\n=== Packet {packet_num} ===")
            print(f"Source:      {packet['source']}")
            print(f"CPU Index:   {packet['cpu_index'] if packet['cpu_index'] is not None else 'N/A'}")
            print(f"Command ID:  0x{packet['command_id']:012x}")
            print(f"Registers:   {len(packet['registers'])}")
            
            for reg_num, reg_value in sorted(packet['registers'].items()):
                print(f"  reg[{reg_num}] = 0x{reg_value:016x}")
            
            offset += packet['packet_size']
    else:
        # Stream from stdin
        buffer = b''
        packet_num = 0
        chunk_size = 4096
        
        while True:
            chunk = sys.stdin.buffer.read(chunk_size)
            if not chunk:
                # EOF reached
                if len(buffer) > 0:
                    print(f"\nWarning: Trailing {len(buffer)} bytes at end of stream", file=sys.stderr)
                break
            
            buffer += chunk
            
            # Process all complete packets in buffer
            while len(buffer) >= 8:
                packet = decode_packet(buffer)
                if not packet:
                    # Not enough data for complete packet yet
                    break
                
                packet_num += 1
                print(f"\n=== Packet {packet_num} ===")
                print(f"Source:      {packet['source']}")
                print(f"CPU Index:   {packet['cpu_index'] if packet['cpu_index'] is not None else 'N/A'}")
                print(f"Command ID:  0x{packet['command_id']:012x}")
                print(f"Registers:   {len(packet['registers'])}")
                
                for reg_num, reg_value in sorted(packet['registers'].items()):
                    print(f"  reg[{reg_num}] = 0x{reg_value:016x}")
                
                sys.stdout.flush()  # Force output to appear immediately
                
                # Remove processed packet from buffer
                buffer = buffer[packet['packet_size']:]

if __name__ == '__main__':
    main()


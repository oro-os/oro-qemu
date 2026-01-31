#!/usr/bin/env python3
import sys
import os
import struct
import subprocess

if len(sys.argv) != 2:
    print("Error: Missing output directory argument", file=sys.stderr)
    print("Usage: python script.py <output_dir> < input.bin", file=sys.stderr)
    sys.exit(1)

output_dir = sys.argv[1]

if not os.path.isdir(output_dir):
    print(f"Error: Directory '{output_dir}' does not exist", file=sys.stderr)
    sys.exit(1)

frame_num = 1
stdin_data = sys.stdin.buffer

while True:
    # Read width and height (8 bytes each, little-endian)
    header = stdin_data.read(16)
    if len(header) == 0:
        break  # EOF
    if len(header) < 16:
        print(f"Error: Incomplete header at frame {frame_num}", file=sys.stderr)
        sys.exit(1)
    
    width, height = struct.unpack('<QQ', header)
    
    # Read pixel data (width * height * 3 bytes RGB8)
    pixel_count = width * height * 3
    pixel_data = stdin_data.read(pixel_count)
    
    if len(pixel_data) < pixel_count:
        print(f"Error: Incomplete pixel data at frame {frame_num}", file=sys.stderr)
        sys.exit(1)
    
    # Convert to PNG using ImageMagick/GraphicsMagick
    output_file = os.path.join(output_dir, f"{frame_num:06d}.png")
    
    try:
        # Try GraphicsMagick first, fall back to ImageMagick
        cmd = ['gm', 'convert', '-size', f'{width}x{height}', '-depth', '8', 'rgb:-', output_file]
        result = subprocess.run(cmd, input=pixel_data, capture_output=True)
        
        if result.returncode != 0:
            # Try ImageMagick
            cmd = ['convert', '-size', f'{width}x{height}', '-depth', '8', 'rgb:-', output_file]
            result = subprocess.run(cmd, input=pixel_data, capture_output=True, check=True)
        
        print(f"Wrote {output_file} ({width}x{height})", file=sys.stderr)
    except (FileNotFoundError, subprocess.CalledProcessError) as e:
        print(f"Error converting frame {frame_num}: {e}", file=sys.stderr)
        sys.exit(1)
    
    frame_num += 1

print(f"Processed {frame_num - 1} frames", file=sys.stderr)

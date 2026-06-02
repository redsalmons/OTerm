#!/usr/bin/env python3
"""
Create macOS icon from source PNG
Creates an iconset with all required sizes for macOS
"""

import os
import subprocess

# Source image
source_image = "/Users/steven/workspace/ot.png"

# Check if source image exists
if not os.path.exists(source_image):
    print(f"Error: Source image not found: {source_image}")
    exit(1)

# Sizes required for macOS iconset
sizes = [16, 32, 128, 256, 512, 1024]
retina_sizes = [16, 32, 128, 256, 512]

# Create iconset directory
iconset_dir = "OceanTerm.iconset"
os.makedirs(iconset_dir, exist_ok=True)

# Generate PNGs for each size using sips
for size in sizes:
    output = f"{iconset_dir}/icon_{size}x{size}.png"
    subprocess.run(['sips', '-s', 'format', 'png', '-z', str(size), str(size), 
                   source_image, '--out', output], capture_output=True)
    print(f"Created {output}")

# Generate @2x versions for Retina
for size in retina_sizes:
    output = f"{iconset_dir}/icon_{size}x{size}@2x.png"
    subprocess.run(['sips', '-s', 'format', 'png', '-z', str(size*2), str(size*2), 
                   source_image, '--out', output], capture_output=True)
    print(f"Created {output}")

# Convert iconset to icns
subprocess.run(['iconutil', '-c', 'icns', iconset_dir])
print(f"Created OceanTerm.icns")

# Remove iconset directory (icns file is created separately)
subprocess.run(['rm', '-rf', iconset_dir])

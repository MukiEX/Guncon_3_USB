#!/usr/bin/env python3
"""Gzip the config page and emit it as a C header: webpage_gz[] / webpage_gz_len."""
import gzip, sys

src, dst = sys.argv[1], sys.argv[2]
data = gzip.compress(open(src, 'rb').read(), compresslevel=9, mtime=0)
with open(dst, 'w') as f:
    f.write('// Generated from web/index.html by tools/embed_web.py -- do not edit\n#pragma once\n#include <stdint.h>\n')
    f.write(f'static const uint32_t webpage_gz_len = {len(data)};\n')
    f.write('static const uint8_t webpage_gz[] = {\n')
    for i in range(0, len(data), 20):
        f.write('  ' + ','.join(f'0x{b:02x}' for b in data[i:i+20]) + ',\n')
    f.write('};\n')

#!/usr/bin/env python3
"""Wrapper for the ITU reference JBIG2 decoder.

Handles its quirks: .jb2 extension requirement, basename-based I/O,
and "00" suffix on output filename.

Usage: python3 jbig2_ref_wrapper.py <jbig2_binary> <input.jbig2> <output.bmp>
"""

import os
import shutil
import subprocess
import sys
import tempfile

jbig2, input_path, output_path = sys.argv[1], sys.argv[2], sys.argv[3]

tmpdir = tempfile.mkdtemp()
try:
    shutil.copy(input_path, os.path.join(tmpdir, "input.jb2"))
    result = subprocess.run(
        [jbig2, "-i", "input", "-f", "jb2", "-o", "output", "-F", "bmp"],
        cwd=tmpdir, capture_output=True, timeout=5,
    )
    out_file = os.path.join(tmpdir, "output00.bmp")
    if result.returncode == 0 and os.path.exists(out_file):
        shutil.copy(out_file, output_path)
    else:
        sys.stderr.write(result.stderr.decode("utf-8", errors="replace"))
        sys.exit(result.returncode or 1)
finally:
    shutil.rmtree(tmpdir)

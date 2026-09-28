#!/usr/bin/env python3
"""Compare streaming/replay with an independently packaged zstd CLI across mutations.

The oracle judges validity; it cannot establish an exact damaged byte. Location
assertions concern documented structural ranges. Resource failures are counted
separately rather than presented as decoder verdict agreement.
"""
import argparse
import json
import pathlib
import random
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--diagnostic', required=True)
parser.add_argument('--oracle', required=True)
parser.add_argument('--stream', action='store_true', help='Use one-byte incremental input and 17-byte output buffers')
args = parser.parse_args()
diag = str(pathlib.Path(args.diagnostic).resolve())
rng = random.Random(20260928)
counts = dict(valid=0, invalid=0, resource=0)
raw = bytes.fromhex('28b52ffd2003190000616263')
# A libarchive frame with a 4 MiB window and unknown content size.
archive = bytes.fromhex('28b52ffd0060c500008848656c6c6f2c2076616c69646174652120010029513597')
data = bytes(rng.randrange(16) for _ in range(2048))
compressed = subprocess.run([args.oracle, '-q', '--check', '-c'], input=data,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout
with tempfile.TemporaryDirectory(prefix='zstdz-telemetry-') as root:
    path = pathlib.Path(root) / 'case.zst'

    def check(label, frame):
        path.write_bytes(frame)
        reference = subprocess.run([args.oracle, '-q', '-t', str(path)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        observed = subprocess.run([diag] + (['--stream'] if args.stream else []) + [str(path)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert observed.returncode in (0, 1), (label, observed.stderr)
        detail = json.loads(observed.stdout)
        assert 0 <= detail['begin'] <= detail['end'] <= len(frame), (label, detail)
        assert 0 <= detail['detected'] <= len(frame), (label, detail)
        if detail['kind'] == 7:  # ZSTDz_resource: not a corruption verdict.
            assert detail['error'] == 16, (label, detail)  # Only a window-limit exclusion is allowed.
            counts['resource'] += 1
            return
        assert (reference.returncode == 0) == (observed.returncode == 0), (
            label, detail, reference.stderr.decode(errors='replace'))
        counts['invalid' if observed.returncode else 'valid'] += 1

    for name, frame in [('raw', raw), ('libarchive', archive), ('compressed', compressed),
                        ('empty', bytes.fromhex('28b52ffd2000010000')),
                        ('empty-compressed', bytes.fromhex('28b52ffd2000050000')),
                        ('empty-size-mismatch', bytes.fromhex('28b52ffd2001010000')),
                        ('concatenated', raw + archive),
                        ('skippable', bytes.fromhex('502a4d1803000000') + b'xyz' + raw)]:
        check(name, frame)
        for index in range(len(frame)):
            mutant = bytearray(frame)
            mutant[index] ^= 0x80
            check(f'{name}:flip:{index}', mutant)
            check(f'{name}:truncate:{index}', frame[:index])
    for fixture in sorted(pathlib.Path('tests/golden-decompression-errors').glob('*.zst')):
        check(str(fixture), fixture.read_bytes())
assert counts['valid'] > 5 and counts['invalid'] > 100, counts
print('Independent CLI differential:', json.dumps(counts, sort_keys=True))

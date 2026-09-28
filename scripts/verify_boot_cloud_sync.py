#!/usr/bin/env python3
"""Capture and verify one reader-idle upload (requires pyserial).

Start this while Home is visible, then open a book and leave a page untouched.
The script does not read configuration, print credentials, synthesize traffic,
or reset the device. A passing run requires Wi-Fi association only after the
reader is active, a visible pause indicator, strict server confirmation, and
restoration of the reading page without a panic.
"""
import argparse
from pathlib import Path
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=150)
    args = parser.parse_args()
    end = time.monotonic() + args.seconds
    lines = []
    with serial.Serial(args.port, 115200, timeout=0.5) as port, args.log.open('w') as log:
        while time.monotonic() < end:
            text = port.readline().decode('utf-8', errors='replace')
            if text:
                if any(word in text.lower() for word in ('authorization:', 'bearer ', 'password=')):
                    text = '[credential-bearing line suppressed]\n'
                log.write(text)
                log.flush()
                print(text, end='', flush=True)
                lines.append(text)
                if 'Reader page restored after sync' in text:
                    end = min(end, time.monotonic() + 15)
    text = ''.join(lines)
    reader_markers = ('Entering activity: EpubReader', 'Entering activity: TxtReader',
                      'Entering activity: XtcReader', 'Entering activity: Reader')
    reader_positions = [text.index(marker) for marker in reader_markers if marker in text]
    reader_entered = min(reader_positions) if reader_positions else -1
    join_marker = 'Reader-idle join started for saved network:'
    upload_started = 'Uploading statistics to ' in text
    requirements = {
        'no boot-time cloud join': join_marker not in text or reader_entered >= 0
            and reader_entered < text.index(join_marker),
        'visible pause indicator before upload': 'Reader sync indicator displayed; controls paused' in text
            and upload_started
            and text.index('Reader sync indicator displayed; controls paused') < text.index('Uploading statistics to '),
        'confirmed authenticated ingest': 'Statistics synced' in text and 'Reader sync finished: ok' in text,
        'reader page restored': 'Reader sync finished: ok' in text
            and 'Reader page restored after sync' in text
            and text.index('Reader sync finished: ok') < text.index('Reader page restored after sync'),
        'no TLS failure or panic': not any(x in text for x in ('Guru Meditation', 'Upload transport failed',
                                                             'assert failed', 'Stack canary', 'panic_abort')),
    }
    for name, passed in requirements.items():
        print(f'{"PASS" if passed else "FAIL"}: {name}')
    return 0 if all(requirements.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())

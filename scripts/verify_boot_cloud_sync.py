#!/usr/bin/env python3
"""Capture and verify one real boot upload (requires pyserial).

Run immediately after flashing/resetting. Does not read configuration, print
credentials, synthesize traffic, or reset the device itself. A passing run
requires the firmware's HTTP-200 + parsed {ok:true} confirmation, followed by
UI initialization, without a transport error or panic.
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
                if 'Boot sync finished: ok' in text:
                    end = min(end, time.monotonic() + 15)
    text = ''.join(lines)
    requirements = {
        'confirmed authenticated ingest': 'Statistics synced' in text and 'Boot sync finished: ok' in text,
        'display initialized after upload': 'Display initialized' in text and 'Boot sync finished: ok' in text
            and text.index('Boot sync finished: ok') < text.index('Display initialized'),
        'UI resumed': 'Entering activity: Home' in text or 'Entering activity: Reader' in text
            or 'Entering activity: EpubReader' in text,
        'no TLS failure or panic': not any(x in text for x in ('Guru Meditation', 'Upload transport failed',
                                                             'assert failed', 'Stack canary', 'panic_abort')),
    }
    for name, passed in requirements.items():
        print(f'{"PASS" if passed else "FAIL"}: {name}')
    return 0 if all(requirements.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())

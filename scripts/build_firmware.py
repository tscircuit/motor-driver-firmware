#!/usr/bin/env python3
"""Stage a complete board-specific filesystem image without device settings."""
import argparse
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]


def build(board, destination):
    source = ROOT / 'firmware'
    profiles = {p.stem for p in (source / 'boards').glob('*.py') if p.stem != '__init__'}
    if board not in profiles:
        raise ValueError('Unknown board. Available: ' + ', '.join(sorted(profiles)))
    destination = Path(destination).resolve()
    if destination == source or source in destination.parents or destination in source.parents:
        raise ValueError('Output must be outside the firmware source tree')
    if destination.exists() and any(destination.iterdir()):
        raise ValueError('Output directory must be empty; choose a new directory')
    for file in source.rglob('*.py'):
        target = destination / file.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(file, target)
    (destination / 'board_config.py').write_text(
        '"""Generated board selection."""\n'
        f'from boards.{board} import Board, Platform, safe_outputs\n')
    return destination


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--board', default='rp2040_drv8847')
    parser.add_argument('--output', default=str(ROOT / 'build/firmware'))
    args = parser.parse_args()
    print(build(args.board, args.output))

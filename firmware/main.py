"""MicroPython entrypoint. Board/MCU selection lives in board_config.py."""
from board_config import Board, Platform, safe_outputs

platform = Platform()
safe_outputs(platform)

from core.settings import Settings
from core.runtime import run

run(Board(platform), platform, Settings())

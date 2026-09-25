"""Safe board startup, then optional USB naming before enumeration."""
from board_config import Platform, safe_outputs
import device_config

platform = Platform()
safe_outputs(platform)
platform.configure_usb(device_config.load_name())

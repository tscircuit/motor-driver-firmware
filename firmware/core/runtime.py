"""Wire the controller to a transport and watchdog; platform owns the I/O."""
import json
from core.controller import Controller


def run(board, platform, settings):
    controller = None
    try:
        watchdog = platform.watchdog(board.watchdog_ms)
        transport = platform.transport()
        controller = Controller(board, platform, settings,
                                lambda data: transport.write_line(json.dumps(data)))
        while True:
            controller.protect()
            for line in transport.read_lines():
                controller.handle(line)
            controller.tick()
            watchdog.feed()
            platform.clock.sleep_us(100 if controller.mode != 'stopped' else 1000)
    finally:
        if controller is not None:
            controller.close()
        else:
            board.motor.disable()
            board.led.value(0)
            board.buzzer.close()

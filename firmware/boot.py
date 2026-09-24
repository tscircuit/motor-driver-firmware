"""Apply the saved USB name before USB enumerates, keeping built-in CDC."""
import machine
import device_config

# A reset or USB reconfiguration must never energize the motor.
machine.Pin(22, machine.Pin.OUT, value=0)
for pin in (18, 19, 20, 21):
    machine.Pin(pin, machine.Pin.OUT, value=0)

try:
    usb = machine.USBDevice()
    driver = usb.BUILTIN_DEFAULT
    name = device_config.load_name()
    usb.active(False)
    usb.builtin_driver = driver
    usb.config(driver.desc_dev, driver.desc_cfg,
               desc_strs=device_config.usb_strings(driver, name))
    usb.active(True)
    device_config.usb_name = name
except Exception as error:
    device_config.usb_name_error = str(error)
    # Keep a recoverable serial port if custom naming is unavailable.
    try:
        usb.active(False)
        usb.builtin_driver = driver
        usb.config(driver.desc_dev, driver.desc_cfg)
        usb.active(True)
    except Exception:
        pass

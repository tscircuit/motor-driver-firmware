"""Optional MicroPython runtime USB naming; no USB assumptions in the core."""
usb_name = None
usb_name_error = None


def usb_strings(driver, name):
    # Preserve VID, PID, serial number, endpoints, and CDC interfaces.
    strings = {driver.desc_dev[15]: name}
    cfg = driver.desc_cfg
    offset = 0
    while offset < len(cfg):
        size = cfg[offset]
        if size < 2 or offset + size > len(cfg):
            raise ValueError('Invalid built-in USB descriptor')
        kind = cfg[offset + 1]
        if kind == 4 and size >= 9 and cfg[offset + 5] in (2, 10):
            index = cfg[offset + 8]  # CDC interface label
            if index:
                strings[index] = name
        elif kind == 11 and size >= 8 and cfg[offset + 4] == 2:
            index = cfg[offset + 7]  # CDC interface association label
            if index:
                strings[index] = name
        offset += size
    return strings


def configure(machine, name):
    global usb_name, usb_name_error
    usb_name = None
    usb_name_error = None
    try:
        usb = machine.USBDevice()
        driver = usb.BUILTIN_DEFAULT
        usb.active(False)
        usb.builtin_driver = driver
        usb.config(driver.desc_dev, driver.desc_cfg, desc_strs=usb_strings(driver, name))
        usb.active(True)
        usb_name = name
    except Exception as error:
        usb_name_error = str(error)
        try:
            usb.active(False)
            usb.builtin_driver = driver
            usb.config(driver.desc_dev, driver.desc_cfg)
            usb.active(True)
        except Exception:
            pass

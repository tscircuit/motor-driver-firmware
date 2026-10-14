/* The final two 4 KiB sectors belong exclusively to redundant settings. */
ASSERT(__flash_binary_end <= 0x101fe000, "Firmware overlaps native settings sectors")

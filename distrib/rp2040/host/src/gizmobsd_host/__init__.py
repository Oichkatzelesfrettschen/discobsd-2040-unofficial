"""Host tools for the GizmoBSD RP2040 console.

The board is a USB CDC-ACM serial device with no network stack, so the host
it plugs into is the gateway: gizmobsd-term attaches a local terminal,
gizmobsd-web serves the console to a browser, gizmobsd-link publishes a
short URL for it, and gizmobsd-console runs the two servers detached.
gizmobsd-sramdump saves a stuck board's SRAM with picotool and decodes the
kernel state in it.
"""

__version__ = "2.0.0"

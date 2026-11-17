"""Host tools for the DiscoBSD RP2040 console.

The board is a USB CDC-ACM serial device with no network stack, so the host
it plugs into is the gateway: discobsd-term attaches a local terminal,
discobsd-web serves the console to a browser, discobsd-link publishes a
short URL for it, and discobsd-console runs the two servers detached.
discobsd-sramdump saves a stuck board's SRAM with picotool and decodes the
kernel state in it.
"""

__version__ = "1.1.0"

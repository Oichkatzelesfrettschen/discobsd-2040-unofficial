"""Host tools for the DiscoBSD RP2040 console.

The board is a USB CDC-ACM serial device with no network stack, so the host
it plugs into is the gateway: discobsd-term attaches a local terminal,
discobsd-web serves the console to a browser, discobsd-link publishes a
short URL for it, and discobsd-console runs the two servers detached.
"""

__version__ = "1.0.3"

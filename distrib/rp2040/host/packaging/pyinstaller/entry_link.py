"""PyInstaller entry point for gizmobsd-link."""

import sys

from gizmobsd_host.link import main

if __name__ == "__main__":
    sys.exit(main())

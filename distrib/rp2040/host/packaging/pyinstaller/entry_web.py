"""PyInstaller entry point for gizmobsd-web."""

import sys

from gizmobsd_host.web import main

if __name__ == "__main__":
    sys.exit(main())

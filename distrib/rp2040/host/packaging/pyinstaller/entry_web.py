"""PyInstaller entry point for discobsd-web."""

import sys

from discobsd_host.web import main

if __name__ == "__main__":
    sys.exit(main())

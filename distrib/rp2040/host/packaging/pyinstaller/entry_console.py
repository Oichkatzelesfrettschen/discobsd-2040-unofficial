"""PyInstaller entry point for discobsd-console."""

import sys

from discobsd_host.console import main

if __name__ == "__main__":
    sys.exit(main())

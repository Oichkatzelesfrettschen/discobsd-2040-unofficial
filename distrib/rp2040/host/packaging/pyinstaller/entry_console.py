"""PyInstaller entry point for gizmobsd-console."""

import sys

from gizmobsd_host.console import main

if __name__ == "__main__":
    sys.exit(main())

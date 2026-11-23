"""PyInstaller entry point for gizmobsd-term."""

import sys

from gizmobsd_host.term import main

if __name__ == "__main__":
    sys.exit(main())

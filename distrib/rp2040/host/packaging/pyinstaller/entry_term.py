"""PyInstaller entry point for discobsd-term."""

import sys

from discobsd_host.term import main

if __name__ == "__main__":
    sys.exit(main())

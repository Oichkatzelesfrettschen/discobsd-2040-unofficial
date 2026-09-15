"""PyInstaller entry point for discobsd-link."""

import sys

from discobsd_host.link import main

if __name__ == "__main__":
    sys.exit(main())

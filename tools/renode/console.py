# Attach to the UART0 socket terminal boot.resc opens (CreateServerSocketTerminal)
# and mirror it to stdout and, optionally, a log file. Keystrokes typed here
# go to the emulated UART0 receive path.
#
# Usage: PYTHON=${PYTHON:-python3} $PYTHON console.py [--host HOST] [--port PORT] [--log FILE]

import argparse
import socket
import sys
import threading


def pump_socket_to_stdout(sock, log):
    while True:
        data = sock.recv(4096)
        if not data:
            break
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
        if log is not None:
            log.write(data)
            log.flush()


def pump_stdin_to_socket(sock):
    while True:
        data = sys.stdin.buffer.read(1)
        if not data:
            break
        sock.sendall(data)


def main():
    parser = argparse.ArgumentParser(description="RP2040 UART0 console client")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=3456)
    parser.add_argument("--log", default=None, help="also append raw bytes here")
    args = parser.parse_args()

    log = open(args.log, "ab") if args.log else None
    sock = socket.create_connection((args.host, args.port))

    reader = threading.Thread(target=pump_socket_to_stdout, args=(sock, log), daemon=True)
    reader.start()
    try:
        if sys.stdin.isatty():
            pump_stdin_to_socket(sock)
        else:
            # No keyboard to forward: stdin at EOF (e.g. redirected from
            # /dev/null) must not tear the socket down out from under the
            # reader thread, which is a daemon and gets no chance to run
            # once main() returns. Block here instead, so this is purely a
            # log-and-mirror client until the socket closes or the caller
            # interrupts it (Ctrl-C, or an external `timeout`).
            reader.join()
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()
        if log is not None:
            log.close()


if __name__ == "__main__":
    main()

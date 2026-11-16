#!/usr/bin/env python3
"""Interactive display for the same 320x320 C renderer, native or QEMU."""

import argparse
import queue
import subprocess
import threading
import time
import tkinter as tk

from build import OUT, build, run_command
from storage import create


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("host", "qemu"), default="host")
    parser.add_argument("--profile", choices=("rom16", "sd32", "sd32gb"), default="rom16")
    parser.add_argument("--scale", type=int, choices=(1, 2, 3), default=2)
    args = parser.parse_args()
    binary = build(args.backend)
    volume = OUT / f"{args.profile}.mzar"
    if not volume.exists():
        create(volume, args.profile, "assets")
    frame = OUT / f"{args.backend}.ppm"
    root = tk.Tk()
    child = subprocess.Popen(
        run_command(args.backend, binary, volume, frame),
        cwd=OUT,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
    )
    events = queue.Queue()

    def reader():
        for line in iter(child.stdout.readline, b""):
            events.put(line)
        events.put(b"exit")

    threading.Thread(target=reader, daemon=True).start()
    root.title(f"MATEzinho | {args.backend} | 320x320 | {args.profile}")
    panel = tk.Label(root, borderwidth=0)
    panel.pack()
    footer = tk.Label(
        root,
        text="Arrows / Enter | Esc: desktop | Tab: tasks | "
        "Ctrl-Tab: chooser | Ctrl-S: save | Ctrl-Q: quit",
    )
    footer.pack()
    mapping = {
        "Left": 256,
        "Right": 257,
        "Up": 258,
        "Down": 259,
        "Escape": 260,
        "Home": 260,
        "F1": 261,
        "F5": 266,
        "Return": 10,
        "BackSpace": 8,
        "Tab": 9,
    }

    pending = queue.Queue()
    busy = True

    def send(key):
        pending.put(key)

    def dispatch():
        nonlocal busy
        if busy or pending.empty():
            return
        key = pending.get()
        if child.poll() is None:
            try:
                child.stdin.write(f"{key}\n".encode())
                child.stdin.flush()
                busy = True
            except BrokenPipeError:
                root.destroy()

    def close():
        if child.poll() is None:
            try:
                child.stdin.write(b"quit\n")
                child.stdin.flush()
            except BrokenPipeError:
                pass
            try:
                child.wait(timeout=2)
            except subprocess.TimeoutExpired:
                child.terminate()
                try:
                    child.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
        root.destroy()

    def pressed(event):
        if event.state & 4:
            if event.keysym.lower() == "q":
                close()
                return "break"
            special = {"s": 265, "w": 262, "Tab": 263}
            if event.keysym in special:
                send(special[event.keysym])
                return "break"
        if event.state & 8 and event.keysym == "Tab":
            send(263)
        elif event.keysym in mapping:
            send(mapping[event.keysym])
        elif event.char and 32 <= ord(event.char) < 127:
            send(ord(event.char))
        return "break"

    def released(event):
        if event.keysym in ("Control_L", "Control_R", "Alt_L", "Alt_R"):
            send(267)

    last_tick = time.monotonic()

    def refresh():
        nonlocal last_tick, busy
        while not events.empty():
            if events.get() == b"exit":
                close()
                return
            picture = tk.PhotoImage(file=str(frame)).zoom(args.scale)
            panel.configure(image=picture)
            panel.image = picture
            busy = False
        now = time.monotonic()
        if now - last_tick >= 1:
            for _ in range(int(now - last_tick)):
                send(264)
            last_tick += int(now - last_tick)
        dispatch()
        root.after(25, refresh)

    root.bind("<KeyPress>", pressed)
    root.bind("<KeyRelease>", released)
    root.protocol("WM_DELETE_WINDOW", close)
    refresh()
    root.mainloop()


if __name__ == "__main__":
    main()

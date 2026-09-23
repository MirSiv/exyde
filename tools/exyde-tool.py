#!/usr/bin/env python3
"""
exyde-tool - small TUI to build, run and inspect the Exyde kernel.

Run from anywhere:

    python3 tools/exyde-tool.py

Keys:
    Up / k       previous item
    Down / j     next item
    Enter        run selected action
    q            quit
"""

import curses
import os
import subprocess
import sys
import time


def find_project_root():
    here = os.path.dirname(os.path.abspath(__file__))
    for _ in range(6):
        if os.path.isfile(os.path.join(here, "Makefile")) and \
           os.path.isdir(os.path.join(here, "src", "kernel")):
            return here
        parent = os.path.dirname(here)
        if parent == here:
            break
        here = parent
    return None


PROJECT_ROOT = find_project_root()
if PROJECT_ROOT is None:
    print("exyde-tool: cannot locate project root "
          "(need Makefile + src/kernel)", file=sys.stderr)
    sys.exit(1)


# (label, command).  command is None for a separator, "QUIT" for exit.
MENU = [
    ("build (make)",                "make"),
    ("rebuild (clean + build)",     "make clean && make"),
    ("run in qemu (25 s)",          "timeout 25 make run"),
    ("build & run",                 "make && timeout 25 make run"),
    ("clean",                       "make clean"),
    ("---",                         None),
    ("git status",                  "git status --short"),
    ("git log (10)",                "git log --oneline -10"),
    ("git branch",                  "git branch -v"),
    ("git diff HEAD",               "git diff HEAD"),
    ("---",                         None),
    ("check architecture boundary",
     "grep -Rn '^\\s*#\\s*include\\s*<arch/' "
     "src/kernel/core/ src/kernel/include/exyde/ ; echo EXIT=$?"),
    ("---",                         None),
    ("quit",                        "QUIT"),
]


# Last-command status, rendered in the footer.
class Status:
    def __init__(self):
        self.text = "ready"
        self.ok = None          # True / False / None
        self.label = ""

    def set_running(self, label):
        self.text = "running: " + label + " ..."
        self.ok = None
        self.label = label

    def set_result(self, label, rc, seconds):
        self.label = label
        if rc == 0:
            self.ok = True
            self.text = "EXIT=0 (SUCCESS)   {}   {:.2f}s".format(
                label, seconds)
        else:
            self.ok = False
            self.text = "EXIT={} (FAILED)   {}   {:.2f}s".format(
                rc, label, seconds)


def git_branch():
    try:
        r = subprocess.run(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            cwd=PROJECT_ROOT, capture_output=True, text=True, timeout=2)
        if r.returncode == 0:
            return r.stdout.strip()
    except Exception:
        pass
    return "?"


def next_selectable(idx, direction):
    n = len(MENU)
    i = (idx + direction) % n
    while MENU[i][1] is None:
        i = (i + direction) % n
    return i


def run_command(stdscr, status, label, cmd):
    status.set_running(label)

    curses.endwin()
    print()
    print("$", cmd)
    print()
    sys.stdout.flush()

    t0 = time.monotonic()
    try:
        rc = subprocess.call(cmd, shell=True, cwd=PROJECT_ROOT)
    except KeyboardInterrupt:
        rc = 130
    dt = time.monotonic() - t0

    print()
    print("[exit code {}]".format(rc))
    try:
        input("press Enter to return to menu...")
    except EOFError:
        pass

    status.set_result(label, rc, dt)
    stdscr.clear()
    stdscr.refresh()


def draw(stdscr, idx, status):
    stdscr.clear()
    h, w = stdscr.getmaxyx()

    title = " Exyde Tool "
    if len(title) < w:
        stdscr.addstr(0, 0, title.center(w, "-"), curses.A_BOLD)

    if h > 3:
        stdscr.addstr(1, 2, ("project: " + PROJECT_ROOT)[:w - 4],
                      curses.A_DIM)
    if h > 4:
        stdscr.addstr(2, 2, ("branch : " + git_branch())[:w - 4],
                      curses.A_DIM)

    y = 4
    for i, (label, cmd) in enumerate(MENU):
        if y >= h - 3:
            break
        if label == "---":
            stdscr.addstr(y, 2, "-" * min(w - 4, 40), curses.A_DIM)
        else:
            prefix = " > " if i == idx else "   "
            text = (prefix + label)[:w - 4]
            attr = curses.A_REVERSE if i == idx else curses.A_NORMAL
            stdscr.addstr(y, 2, text, attr)
        y += 1

    # ---- footer: status line + help line ---------------------------
    if h >= 3:
        bar = " " + status.text + " "
        if status.ok is True:
            attr = curses.A_REVERSE
        elif status.ok is False:
            attr = curses.A_REVERSE | curses.A_BOLD
        else:
            attr = curses.A_DIM
        line = bar[:w - 2].ljust(w - 2)
        try:
            stdscr.addstr(h - 3, 0, line, attr)
        except curses.error:
            pass
    if h >= 2:
        help_text = "up/down navigate    Enter run    q quit"
        try:
            stdscr.addstr(h - 1, 2, help_text[:w - 4], curses.A_DIM)
        except curses.error:
            pass

    stdscr.refresh()


def main(stdscr):
    curses.curs_set(0)
    stdscr.keypad(True)
    try:
        curses.use_default_colors()
    except curses.error:
        pass

    status = Status()
    idx = 0
    if MENU[idx][1] is None:
        idx = next_selectable(idx, +1)

    while True:
        draw(stdscr, idx, status)
        ch = stdscr.getch()

        if ch in (curses.KEY_UP, ord('k')):
            idx = next_selectable(idx, -1)
        elif ch in (curses.KEY_DOWN, ord('j')):
            idx = next_selectable(idx, +1)
        elif ch in (curses.KEY_ENTER, ord('\n'), ord('\r')):
            label, cmd = MENU[idx]
            if cmd == "QUIT":
                return
            run_command(stdscr, status, label, cmd)
        elif ch == ord('q'):
            return


if __name__ == "__main__":
    try:
        curses.wrapper(main)
    except KeyboardInterrupt:
        pass

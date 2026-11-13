#!/usr/bin/env python3
"""Exercise SSH graphics detection and early keystrokes through a real PTY."""

import fcntl
import os
from pathlib import Path
import pty
import re
import select
import struct
import subprocess
import tempfile
import termios
import time
import unittest


ROOT = Path(__file__).resolve().parent.parent
QUERY = re.compile(
    rb"\x1b_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\\|\x1b\[>q|\x1b\[5n|\x1b\[c"
)
IMAGE = {
    "sixel": rb"\x1bP[0-9;]*q",
    "iterm2": rb"\x1b\]1337;File=",
    "kitty": rb"\x1b_G[^;]*a=[Tt][^;]*;",
    "warp": rb"\x1b_G[^;]*a=[Tt][^;]*;",
}
HINT_KEYS = """
TERM_PROGRAM LC_TERMINAL TMUX STY WEZTERM_EXECUTABLE WEZTERM_EXECUTABLE_DIR
WEZTERM_PANE WEZTERM_UNIX_SOCKET KITTY_WINDOW_ID KITTY_PID KITTY_INSTALLATION_DIR
ITERM_SESSION_ID GHOSTTY_RESOURCES_DIR GHOSTTY_BIN_DIR TERMINAL_NAME
EAT_SHELL_INTEGRATION_DIR MLTERM KONSOLE_VERSION CTX_BACKEND VTE_VERSION
ComSpec COMSPEC XTERM_VERSION
""".split()


class TerminalStartupTests(unittest.TestCase):
    def run_terminal(self, mode, early_escape):
        with tempfile.TemporaryDirectory(prefix="pixelterm-terminal-") as directory:
            config = Path(directory) / "config.ini"
            config.write_text("[default]\nprotocol=auto\n", encoding="utf-8")
            env = os.environ.copy()
            for key in HINT_KEYS:
                env.pop(key, None)
            env.update(TERM="xterm-256color", SSH_TTY="/dev/pts/1")
            if mode == "warp":
                env["TERM_PROGRAM"] = "WarpTerminal"
            master, slave = pty.openpty()
            original = termios.tcgetattr(slave)
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 800, 480))
            process = None
            try:
                process = subprocess.Popen(
                    [str(ROOT / "bin/pixelterm"), "--config", str(config),
                     "--preload", "false", str(ROOT / "screenshots/1.png")],
                    stdin=slave, stdout=slave, stderr=slave, env=env, cwd=ROOT,
                )
                output = bytearray()
                pending = b""
                injected = False
                rendered = False
                next_escape = 0.0
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    ready, _, _ = select.select([master], [], [], 0.05)
                    if ready:
                        chunk = os.read(master, 65536)
                        output.extend(chunk)
                        pending += chunk
                        while (match := QUERY.search(pending)) is not None:
                            request = match.group()
                            pending = pending[match.end():]
                            if early_escape and not injected:
                                os.write(master, b"\x1b")
                                injected = True
                            if request == b"\x1b[c":
                                reply = b"\x1b[?62;4;22c" if mode == "sixel" else b"\x1b[?62;22c"
                            elif request == b"\x1b[>q":
                                reply = b"\x1bP>|iTerm2 3.5.0\x1b\\" if mode == "iterm2" else b"\x1bP>|unknown\x1b\\"
                            elif request == b"\x1b[5n":
                                reply = b"\x1b[0n"
                            else:
                                reply = b"\x1b_Gi=31;OK\x1b\\" if mode == "kitty" else b""
                            if reply:
                                os.write(master, reply)
                        pending = pending[-100:]
                        if mode in IMAGE:
                            rendered = rendered or re.search(IMAGE[mode], output) is not None
                    if process.poll() is not None:
                        break
                    if not early_escape and rendered and time.monotonic() >= next_escape:
                        os.write(master, b"\x1b")
                        next_escape = time.monotonic() + 0.3
                self.assertIsNotNone(process.poll(), "Escape was lost during startup")
                self.assertEqual(process.returncode, 0)
                if mode in IMAGE:
                    self.assertTrue(rendered, "Expected image protocol was not emitted")
                if early_escape:
                    self.assertTrue(injected)
                restored = termios.tcgetattr(slave)
                # Kernels may mark buffered canonical input for reprocessing.
                restored[3] &= ~getattr(termios, "PENDIN", 0)
                original[3] &= ~getattr(termios, "PENDIN", 0)
                self.assertEqual(restored, original)
            finally:
                if process is not None and process.poll() is None:
                    process.kill()
                    process.wait()
                os.close(master)
                os.close(slave)

    def test_protocol_output(self):
        for mode in IMAGE:
            with self.subTest(mode=mode):
                self.run_terminal(mode, early_escape=False)

    def test_escape_during_probe_is_replayed(self):
        for mode in (*IMAGE, "unknown"):
            with self.subTest(mode=mode):
                self.run_terminal(mode, early_escape=True)


if __name__ == "__main__":
    unittest.main()

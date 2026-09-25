"""Exercise the actual interactive editor through a pseudo-terminal."""
import os
import errno
import pathlib
import pty
import select
import subprocess
import sys
import time
import tempfile
import unittest
import fcntl
import signal
import struct
import termios
from vt_screen import Screen

EXE = str(pathlib.Path(sys.argv.pop(1)).resolve())


class TerminalTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.start("--no-state")

    def start(self, *args):
        self.master, slave = pty.openpty()
        self.screen = Screen(12,60)
        fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack("HHHH",12,60,0,0))
        env = dict(os.environ, TERM="xterm", INPUTRC="/dev/null", XDG_CONFIG_HOME=self.folder.name)
        self.process = subprocess.Popen(
            [EXE, *args], stdin=slave, stdout=slave, stderr=slave, env=env
        )
        os.close(slave)
        self.pending = b""
        self.expect(b"> ")

    def tearDown(self):
        self.stop()
        self.folder.cleanup()

    def stop(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=5)
        os.close(self.master)

    def restart(self, *args):
        self.send(b"exit\n")
        self.assertEqual(self.wait_for_exit(), 0)
        self.stop()
        self.start(*args)

    def wait_for_exit(self):
        # Keep consuming terminal output during shutdown. On macOS, terminal
        # restoration can wait for output to drain before the child can exit.
        deadline = time.monotonic() + 5
        while self.process.poll() is None:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                self.fail(f"Process did not exit; received {self.pending!r}")
            if select.select([self.master], [], [], min(remaining, 0.05))[0]:
                try:
                    if not self.receive():
                        return self.process.wait(timeout=max(0.001, deadline-time.monotonic()))
                except OSError as error:
                    if error.errno != errno.EIO:  # Linux PTYs report EIO at EOF.
                        raise
                    return self.process.wait(timeout=max(0.001, deadline-time.monotonic()))
        return self.process.returncode

    def send(self, text):
        os.write(self.master, text)

    def receive(self):
        data = os.read(self.master,65536)
        self.screen.feed(data)
        self.pending += data
        return data

    def until_screen(self, condition):
        deadline = time.monotonic() + 5
        while not condition():
            remaining = deadline-time.monotonic()
            if remaining <= 0:
                self.fail(f"Screen condition timed out: {self.screen.lines!r}; cursor={self.screen.row,self.screen.column}")
            if select.select([self.master],[],[],remaining)[0]: self.receive()

    def expect(self, text):
        deadline = time.monotonic() + 5
        while text not in self.pending:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                self.fail(f"Expected {text!r}; received {self.pending!r}")
            if select.select([self.master], [], [], remaining)[0]:
                try:
                    self.receive()
                except OSError:
                    self.fail(f"Terminal closed waiting for {text!r}: {self.pending!r}")
        before, self.pending = self.pending.split(text, 1)
        return before

    def calculate(self, expression, answer):
        self.send(expression + b"\n")
        self.expect(b"ans = " + answer + b"\r\n")
        self.expect(b"> ")

    def test_up_down_history_and_draft(self):
        self.calculate(b"10+1", b"11")
        self.calculate(b"20+2", b"22")
        self.send(b"\x1b[A")
        self.expect(b"20+2")  # Recalled before Enter.
        self.send(b"\x1b[A\n")
        self.expect(b"ans = 11\r\n")
        self.expect(b"> ")
        self.send(b"\x1b[A\x1b[A\x1b[B\n")
        self.expect(b"ans = 11\r\n")  # Down selects the newer history entry.
        self.expect(b"> ")
        self.send(b"123\x1b[A\x1b[B\n")
        self.expect(b"ans = 123\r\n")  # Down restores the unfinished draft.

    def test_application_mode_arrows(self):
        self.calculate(b"7+1", b"8")
        self.calculate(b"9+1", b"10")
        self.send(b"\x1bOA\x1bOA\x1bOB\n")
        self.expect(b"ans = 10\r\n")

    def test_default_degree_preview_and_result(self):
        self.send(b"sin(360)")
        self.until_screen(lambda: self.screen.lines[11] == "= 0")
        self.send(b"\n")
        self.expect(b"ans = 0\r\n")
        self.expect(b"> ")
        self.calculate(b"cos(360)", b"1")
        self.send(b"mode rad\n")
        self.expect(b"> ")
        self.calculate(b"sin(pi/2)", b"1")

    def test_operator_expansion_is_visible_before_enter(self):
        self.calculate(b"20", b"20")
        for operator, operand, answer in [
            (b"+", b"2", b"22"),
            (b"--", b"3", b"19"),
            (b"*", b"2", b"38"),
            (b"/", b"2", b"19"),
        ]:
            with self.subTest(operator=operator):
                self.send(operator)
                displayed_operator = b"-" if operator == b"--" else operator
                self.until_screen(lambda: self.screen.lines[10] == "> ans " + displayed_operator.decode())
                self.send(operand + b"\n")
                self.expect(b"ans = " + answer + b"\r\n")
                self.expect(b"> ")
        self.send(b"\x1b[A")
        self.expect(b"ans /2")  # Store the expanded expression in history.

    def test_operator_expansion_ignores_leading_whitespace(self):
        self.calculate(b"20", b"20")
        for whitespace, operator, operand, answer in [
            (b"     ", b"+", b"2", b"22"),
            (b" ", b"--", b"3", b"19"),
            (b"\x16\t", b"*", b"2", b"38"),  # Ctrl-V inserts a literal tab.
            (b"  \x16\t ", b"/", b"2", b"19"),
        ]:
            with self.subTest(operator=operator):
                self.send(whitespace + operator)
                displayed_operator = b"-" if operator == b"--" else operator
                self.until_screen(lambda: self.screen.lines[10] == "> ans " + displayed_operator.decode())
                self.send(operand + b"\n")
                self.expect(b"ans = " + answer + b"\r\n")
                self.expect(b"> ")
                # Print the recalled expression inside brackets to verify that
                # whitespace was removed from the actual buffer, not just hidden.
                self.send(b"\x1b[A\x01echo [\x05]\n")
                self.expect(b"[ans " + displayed_operator + operand + b"]\r\n")
                self.assertIn("[ans " + (displayed_operator + operand).decode() + "]",self.screen.lines)
                self.expect(b"> ")

    def test_normal_operators_and_negative_literals(self):
        self.calculate(b"12-5", b"7")
        self.calculate(b"2*-3", b"-6")
        self.calculate(b"(-2)", b"-2")
        self.send(b"5\x01-\n")  # Insert at start of a nonempty line.
        self.expect(b"ans = -5\r\n")

    def test_single_minus_starts_negative_and_double_minus_uses_ans(self):
        for whitespace in (b"", b"     "):
            with self.subTest(whitespace=whitespace):
                self.calculate(b"20",b"20")
                self.send(whitespace + b"-")
                self.until_screen(lambda: self.screen.lines[10] == "> " + whitespace.decode() + "-")
                self.assertNotIn("ans",self.screen.lines[10])
                self.send(b"3")
                self.until_screen(lambda: self.screen.lines[11] == "= -3")
                self.send(b"\n")
                self.expect(b"ans = -3\r\n")
                self.expect(b"> ")
                self.calculate(b"20",b"20")
                self.send(whitespace + b"-")
                self.until_screen(lambda: self.screen.lines[10] == "> " + whitespace.decode() + "-")
                self.send(b"-")
                self.until_screen(lambda: self.screen.lines[10] == "> ans -")
                self.send(b"3")
                self.until_screen(lambda: self.screen.lines[11] == "= 17")
                self.send(b"\n")
                self.expect(b"ans = 17\r\n")
                self.expect(b"> ")

    def test_negative_expressions_and_inner_double_minus(self):
        self.calculate(b"20",b"20")
        self.calculate(b"-sqrt(9)",b"-3")
        self.calculate(b"-.5",b"-0.5")
        self.calculate(b"4--3",b"7")
        self.calculate(b"2*-3",b"-6")
        self.calculate(b"  \x16\t--2",b"-8")

    def test_cleared_line_expansion_and_error_recovery(self):
        self.calculate(b"40", b"40")
        self.send(b"junk\x15+")  # Ctrl-U empties the editor.
        self.expect(b"ans +")
        self.send(b"2\n")
        self.expect(b"ans = 42\r\n")
        self.expect(b"> ")
        self.send(b"/0\n")
        self.expect(b"error: Division by zero.")
        self.expect(b"> ")
        self.calculate(b"+1", b"43")
        self.send(b"\x04")
        self.assertEqual(self.wait_for_exit(), 0)

    def test_history_persists_and_ctrl_r_searches(self):
        self.restart()
        self.calculate(b"345+6", b"351")
        self.calculate(b"99+1", b"100")
        self.restart()
        self.send(b"\x12" + b"345")
        self.expect(b"(reverse-i-search)")
        self.send(b"\x05\n")  # Accept the match with Ctrl-E, then execute it.
        self.expect(b"ans = 351\r\n")
        history = pathlib.Path(self.folder.name) / "clicalc" / "history"
        self.assertIn("345+6", history.read_text())
        self.assertIn("99+1", history.read_text())

    def test_no_history_does_not_write(self):
        self.restart("--no-history")
        self.calculate(b"23+4", b"27")
        self.assertFalse((pathlib.Path(self.folder.name) / "clicalc" / "history").exists())

    def test_completion_commands_functions_variables_and_units(self):
        self.send(b"sigf\t6\n")
        self.expect(b"> ")
        self.calculate(b"sq\t81)", b"9")
        self.send(b"supply_voltage=12\n")
        self.expect(b"supply_voltage = 12\r\n")
        self.expect(b"> ")
        self.calculate(b"supply_v\t*2", b"24")
        self.send(b"parallel(x,y)=x*y/(x+y)\n")
        self.expect(b"parallel(x,y) = x*y/(x+y)\r\n")
        self.expect(b"> ")
        self.calculate(b"parall\t10,10)", b"5")
        self.calculate(b"10 in -> centimete\t", b"25.4")
        self.send(b"base he\t\n")
        self.expect(b"ans = 0x19.")

    def test_live_preview_does_not_commit(self):
        self.calculate(b"20", b"20")
        self.send(b"x=21*2")
        self.expect(b"= 42")  # No Enter has been sent.
        self.send(b"\x15ans\n")
        self.expect(b"ans = 20\r\n")
        self.expect(b"> ")
        self.send(b"x\n")
        self.expect(b"Unknown variable 'x'.")
        self.expect(b"> ")
        self.send(b"6*7")
        self.expect(b"= 42")
        self.send(b"\n")
        self.expect(b"ans = 42\r\n")

    def test_preview_can_be_disabled(self):
        self.send(b"preview off\n")
        self.expect(b"preview = off\r\n")
        self.expect(b"> ")
        self.send(b"6*7")
        deadline = time.monotonic() + 0.35
        received = b""
        while time.monotonic() < deadline:
            if select.select([self.master], [], [], deadline-time.monotonic())[0]:
                received += self.receive()
        self.assertNotIn(b"= 42", received)
        self.send(b"\n")
        self.expect(b"ans = 42\r\n")

    def test_optional_session_restore(self):
        self.restart("--restore-session")
        self.send(b"saved_value=255; mode deg; sigfigs 12; base hex; programmer 16 unsigned\n")
        self.expect(b"programmer = 16 unsigned\r\n")
        self.expect(b"> ")
        self.restart("--restore-session")
        self.send(b"saved_value\n")
        self.expect(b"ans = 0x00FF\r\n")
        self.expect(b"> ")
        self.send(b"mode\n")
        self.expect(b"mode = deg\r\n")

    def test_bottom_preview_does_not_scroll_when_validity_changes(self):
        for n in range(15): self.calculate(str(n).encode(),str(n).encode())
        self.assertEqual(self.screen.bottom,10)
        self.assertEqual(self.screen.row,10)
        transcript = self.screen.lines[:10]
        scrolls = self.screen.scrolls
        self.send(b"6*7")
        self.until_screen(lambda: self.screen.lines[11] == "= 42")
        self.assertEqual(self.screen.lines[10],"> 6*7")
        self.assertEqual(self.screen.row,10)
        for _ in range(3):
            self.send(b"+")
            self.until_screen(lambda: self.screen.lines[11] == "")
            self.assertEqual(self.screen.row,10)
            self.assertEqual(self.screen.lines[:10],transcript)
            self.assertEqual(self.screen.scrolls,scrolls)
            self.send(b"\x7f")
            self.until_screen(lambda: self.screen.lines[11] == "= 42")
        self.assertEqual(self.screen.scrolls,scrolls)
        self.assertEqual(self.screen.lines[:10],transcript)

    def test_ctrl_l_and_cls_clear_same_area(self):
        self.calculate(b"42",b"42")
        self.send(b"12+3")
        self.until_screen(lambda: self.screen.lines[11] == "= 15")
        self.send(b"\x0c")
        self.until_screen(lambda: self.screen.lines == [""]*10 + ["> 12+3","= 15"] and self.screen.row == 10)
        self.assertEqual(self.screen.lines[10],"> 12+3")
        self.assertEqual(self.screen.lines[11],"= 15")
        self.assertEqual(self.screen.row,10)
        self.send(b"\x15cls\n")
        self.until_screen(lambda: self.screen.lines == [""]*10 + [">", ""])
        self.calculate(b"ans",b"42")

    def test_preview_region_resize_toggle_and_exit(self):
        self.send(b"1/7")
        self.until_screen(lambda: self.screen.lines[11].startswith("= 0.142857"))
        self.screen.resize(8,30)
        fcntl.ioctl(self.master,termios.TIOCSWINSZ,struct.pack("HHHH",8,30,0,0))
        os.kill(self.process.pid,signal.SIGWINCH)
        self.until_screen(lambda: self.screen.bottom == 6 and self.screen.lines[7].endswith("..."))
        self.assertEqual(self.screen.row,6)
        self.assertEqual(self.screen.lines[6],"> 1/7")
        self.screen.resize(14,60)
        fcntl.ioctl(self.master,termios.TIOCSWINSZ,struct.pack("HHHH",14,60,0,0))
        os.kill(self.process.pid,signal.SIGWINCH)
        self.until_screen(lambda: self.screen.bottom == 12 and self.screen.lines[13].startswith("= 0.142857"))
        self.assertFalse(any(line.startswith("= ") for line in self.screen.lines[:13]))
        self.assertEqual(self.screen.row,12)
        self.send(b"\x15preview off\n")
        self.until_screen(lambda: self.screen.bottom == 13 and self.screen.lines[13] == "")
        self.send(b"preview on\n")
        self.until_screen(lambda: self.screen.bottom == 12 and self.screen.lines[12] == ">")
        self.send(b"\x04")
        self.until_screen(lambda: self.screen.bottom == 13)
        self.assertEqual(self.process.wait(timeout=5),0)

    def test_format_completion_and_si_preview(self):
        self.send(b"forma\teng\n")
        self.expect(b"> ")
        self.send(b"12345")
        self.until_screen(lambda: self.screen.lines[11] == "= 12.345k")
        self.assertEqual(self.screen.lines[10],"> 12345")
        self.send(b"\n")
        self.expect(b"ans = 12.345k\r\n")

    def test_wrapped_input_keeps_preview_outside_scroll_region(self):
        self.send(b"1+"*40 + b"1")
        self.until_screen(lambda: self.screen.lines[11] == "= 41")
        self.assertEqual(self.screen.bottom,10)
        self.assertLess(self.screen.row,11)
        self.send(b"\n")
        self.expect(b"ans = 41\r\n")
        self.expect(b"> ")
        self.assertEqual(self.screen.lines[11],"")

    def test_signal_restores_scroll_region(self):
        self.send(b"6*7")
        self.until_screen(lambda: self.screen.lines[11] == "= 42")
        self.process.send_signal(signal.SIGTERM)
        self.until_screen(lambda: self.screen.bottom == 11 and self.screen.lines[11] == "")
        self.process.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()

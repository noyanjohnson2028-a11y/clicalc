import pathlib
import os
import subprocess
import sys
import tempfile
import unittest

EXE = str(pathlib.Path(sys.argv.pop(1)).resolve())

class CliTests(unittest.TestCase):
    def run_calc(self, *args, input=None):
        return subprocess.run([EXE, *args], input=input, text=True, capture_output=True, timeout=10)

    def test_expression(self):
        result = self.run_calc("2^10+sqrt(16)")
        self.assertEqual((result.returncode, result.stdout, result.stderr), (0, "ans = 1028\n", ""))

    def test_pipe(self):
        result = self.run_calc(input="x=255;\nbase hex\nx\nvlist\nexit\n1/0\n")
        self.assertEqual(result.returncode, 0)
        self.assertIn("x = 0xFF\n", result.stdout)
        self.assertNotIn("> ", result.stdout)
        self.assertNotIn("clicalc 0.1", result.stdout)

    def test_script_and_errors(self):
        with tempfile.TemporaryDirectory() as folder:
            script = pathlib.Path(folder) / "example.calc"
            script.write_text("# example\na=2;\na*3\n1/0\n100\n")
            result = self.run_calc("-f", str(script))
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "ans = 6\n")
            self.assertIn(":4: error: Division by zero", result.stderr)

    def test_flags(self):
        result = self.run_calc("--base", "hex", "-e", "255", "-e", "base bin")
        self.assertEqual(result.stdout, "ans = 0xFF\nans = 0b11111111\n")
        self.assertEqual(self.run_calc("--precision", "100", "1/7").returncode, 0)
        self.assertEqual(self.run_calc("--base").returncode, 1)
        self.assertEqual(self.run_calc("--wat").returncode, 1)
        self.assertEqual(self.run_calc("--", "-sqrt(4)").stdout, "ans = -2\n")

    def test_persistence(self):
        with tempfile.TemporaryDirectory() as folder:
            state = str(pathlib.Path(folder) / "definitions.calc")
            result = self.run_calc("--state", state, "-e", "double(x)=2*x", "-e", "unit furlong = 201.168 m")
            self.assertEqual(result.returncode, 0, result.stderr)
            result = self.run_calc("--state", state, "-e", "double(21)", "-e", "1 furlong -> m")
            self.assertEqual(result.stdout, "ans = 42\nans = 201.168\n")
            result = self.run_calc("--state", state, "--no-state", "double(2)")
            self.assertEqual(result.returncode, 1)
            self.assertIn("Unknown function", result.stderr)

    def test_invalid_state_does_not_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            state = pathlib.Path(folder) / "definitions.calc"
            original = "echo unexpected\n"
            state.write_text(original)
            result = self.run_calc("--state", str(state), "1+1")
            self.assertEqual(result.returncode, 1)
            self.assertEqual(state.read_text(), original)

    def test_existing_definitions_can_be_updated(self):
        with tempfile.TemporaryDirectory() as folder:
            state = str(pathlib.Path(folder) / "definitions.calc")
            for expression in ("double(x)=2*x", "triple(x)=3*x"):
                result = self.run_calc("--state", state, expression)
                self.assertEqual(result.returncode, 0, result.stderr)
            result = self.run_calc("--state", state, "double(3)+triple(3)")
            self.assertEqual(result.stdout, "ans = 15\n")
            self.assertEqual(result.returncode, 0, result.stderr)

    @unittest.skipUnless(os.name == "nt", "Windows settings directory")
    def test_windows_appdata_session(self):
        with tempfile.TemporaryDirectory() as folder:
            env = dict(os.environ, APPDATA=folder)
            env.pop("XDG_CONFIG_HOME", None)
            for expression, answer in (("x=12", "x = 12\n"), ("x=x+1", "x = 13\n"), ("x", "ans = 13\n")):
                result = subprocess.run([EXE, "--restore-session", expression], env=env,
                                        text=True, capture_output=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, answer)
            self.assertTrue((pathlib.Path(folder) / "clicalc" / "session.calc").exists())

    def test_help_and_errors(self):
        self.assertIn("Usage:", self.run_calc("--help").stdout)
        self.assertIn("vlist", self.run_calc("-e", "help").stdout)
        self.assertEqual(self.run_calc("-f", "/nonexistent/calc").returncode, 1)
        result = self.run_calc("sqrt(-1)")
        self.assertEqual(result.returncode, 1)
        self.assertIn("error:", result.stderr)

    def test_full_session_restore(self):
        with tempfile.TemporaryDirectory() as folder:
            session = pathlib.Path(folder) / "session.calc"
            result = self.run_calc("--session", str(session), "-e",
                                   "x=1/7; mode deg; sigfigs 75; base hex; preview off")
            self.assertEqual(result.returncode, 0, result.stderr)
            result = self.run_calc("--session", str(session), "-e", "base", "-e", "mode", "-e", "sigfigs", "-e", "preview", "-e", "base dec; x*7")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("base = 16\nmode = deg\nsigfigs = 75\npreview = off\n", result.stdout)
            self.assertTrue(result.stdout.endswith("ans = 1\n"))
            result = self.run_calc("--session", str(session), "--no-state", "x")
            self.assertEqual(result.returncode, 1)

    def test_invalid_session_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as folder:
            session = pathlib.Path(folder) / "session.calc"
            original = "clicalc-session 99\n"
            session.write_text(original)
            result = self.run_calc("--session", str(session), "1+1")
            self.assertEqual(result.returncode, 1)
            self.assertEqual(session.read_text(), original)

    def test_error_caret_and_programmer_output(self):
        result = self.run_calc("  x=12 + sqrt(")
        self.assertEqual(result.returncode, 1)
        self.assertIn("  " + "  x=12 + sqrt(" + "\n", result.stderr)
        self.assertEqual(result.stderr.splitlines()[-1].index("^"), 2 + len("  x=12 + sqrt("))
        result = self.run_calc("-e", "programmer 8 signed; 255; bases")
        self.assertEqual(result.stdout, "dec = -1\nhex = 0xFF\nbin = 0b11111111\n")
        self.assertEqual(self.run_calc("32 degF -> degC").stdout, "ans = 0\n")

    def test_format_command_and_si_engineering_output(self):
        result = self.run_calc("-e", "format eng; sigfigs 6; 12345", "-e", "0.0000012", "-e", "format")
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(result.stdout,"ans = 12.345k\nans = 1.2u\nformat = eng\n")
        self.assertIn("format auto",self.run_calc("help").stdout)
        self.assertNotIn("scimode",self.run_calc("help").stdout)

if __name__ == "__main__":
    unittest.main()

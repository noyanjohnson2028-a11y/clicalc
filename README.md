# clicalc

A terminal calculator with a reusable C++17 engine. Inspired by the behavior in
the [ZoeSoft Console Calculator manual](https://www.zoesoft.com/console-calculator/ccalc-manual/).
This is a new implementation, with no dependency on ZoeSoft code.

- High-precision scientific math, variables, and custom functions.
- Decimal, binary, and hexadecimal output, plus fixed-width programmer modes.
- SI prefixes and unit conversion, including Celsius and Fahrenheit.
- Command history, Tab completion, and a live preview on a reserved bottom line.
- Scripts, optional session restore, and a UI-independent engine for future Apple apps.

**Contents:** [Build and run](#build-and-run) · [Tutorial](#tutorial) ·
[Command-line help](#command-line-help) · [Expressions](#expressions) ·
[Commands](#commands) · [Programmer mode](#programmer-mode) ·
[Unit conversion](#unit-conversion) · [Persistence](#persistence-and-scripts) ·
[Development](#development) · [License](#license)

## Build and run

For Windows, see [Windows setup](#windows-setup). The instructions immediately
below are for macOS and Linux.

You need a C++17 compiler, Make, and the development headers/libraries for
**MPFR**, **GMP** (including `gmpxx.h`), and **GNU Readline**. Python 3 runs the CLI
and interactive terminal integration tests.

```sh
# macOS (Homebrew)
brew install gmp mpfr readline
make CPPFLAGS="-Iinclude -I$(brew --prefix)/include -I$(brew --prefix readline)/include" LDFLAGS="-L$(brew --prefix)/lib -L$(brew --prefix readline)/lib"

# Debian / Ubuntu
sudo apt install build-essential libmpfr-dev libgmp-dev libreadline-dev python3
make

# Arch Linux
sudo pacman -S --needed base-devel mpfr gmp readline python
make
```

```sh
./build/clicalc                    # interactive terminal
./build/clicalc '2^10 + sqrt(16)'  # ans = 1028
./build/clicalc --base hex '255'   # ans = 0xFF
./build/clicalc -f examples/tour.calc
printf 'x=12;\nx*3\n' | ./build/clicalc
make test
```

The default frontend uses GNU Readline for line editing. **Up/Down** browse saved
command history, and **Ctrl-R** searches it. Down past the newest entry restores the unfinished
line. Typing **`+`, `/`, or `*` on an empty prompt immediately inserts `ans `**
before the operator, so the complete expression is visible and editable before
pressing Enter. Leading whitespace is discarded: typing `     +` displays `ans +`.
A single leading **`-` stays a minus sign**, so `-3` starts a new calculation and
evaluates to `-3`. Typing a second consecutive minus changes **`--` to `ans -`**.
For example, after calculating `20`, typing `--3` displays `ans -3` and evaluates
to `17`. This also works after leading spaces or tabs.

**Tab** completes commands, built-in/custom functions, variables, units, and
command options. A uniquely completed function also inserts `(`. **Ctrl-L** clears
the screen just like `cls`, retaining the current expression being edited.
**Live preview** appears below the input after a brief typing pause:

```text
> 6*7
= 42
```

With preview enabled, the bottom terminal row is always reserved for it, and
the input/transcript scroll above that row. The reserved row stays blank for an
empty or incomplete expression, so computing or clearing a preview never shifts
the input or transcript. Long previews are truncated to fit without wrapping;
Enter prints the full answer. Resizing the terminal updates the reserved area.
`preview off` or `--no-preview` releases the reserved row and disables
it. Preview never changes `ans`, variables, definitions, settings, or the random
sequence; even an assignment is speculative until submitted.

A minimal standard-input frontend can still be built without Readline; it lacks
arrow-key history, completion, live previews, and live operator insertion:

```sh
make WITH_READLINE=0
# Return to the default editor (no clean rebuild required):
make
```

Readline is a CLI-only dependency. Its GPL licensing should be accounted for if
distributing the terminal frontend. The core uses MPFR/GMP and does not link
Readline.

CMake 3.18+ is also supported:

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
# Add -DCLICALC_READLINE=OFF for the minimal standard-input frontend.
# On macOS, use -DCMAKE_PREFIX_PATH="$(brew --prefix)" as needed.
```

Install with `make install PREFIX="$HOME/.local"` or `cmake --install build-cmake`.

### Windows setup

The same repository builds a native 64-bit Windows executable using
[MSYS2 UCRT64](https://www.msys2.org/). Windows does not require WSL.
This build uses the basic input frontend: all calculator math, degree/radian
modes, variables, custom functions, units, scripts, and session restore are
available. Readline's live preview, Tab completion, persistent command history,
and automatic visible `ans` insertion are not included. Type `exit` to quit.

1. Install MSYS2, open **MSYS2 UCRT64** from the Start menu, and update it:

   ```sh
   pacman -Syu
   ```

   If asked to close the terminal, reopen **MSYS2 UCRT64** and run that command
   again to finish updating.

2. Install the compiler, build tools, math libraries, and test runner:

   ```sh
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-gmp mingw-w64-ucrt-x86_64-mpfr mingw-w64-ucrt-x86_64-python
   ```

3. Download and extract this repository, then change to its folder in that
   terminal. For example, replace `YOUR_NAME` and the folder name as needed:

   ```sh
   cd /c/Users/YOUR_NAME/Downloads/clicalc-main
   cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build-windows
   ctest --test-dir build-windows --output-on-failure
   ```

4. Start the calculator:

   ```sh
   ./build-windows/clicalc.exe
   ```

You can also run it from **PowerShell**. The math and compiler runtime DLLs
remain in MSYS2, so add its UCRT64 directory to that terminal's PATH first
(adjust `C:\msys64` if you installed MSYS2 elsewhere):

```powershell
$env:Path = "C:\msys64\ucrt64\bin;" + $env:Path
cd "$HOME\Downloads\clicalc-main"
.\build-windows\clicalc.exe
.\build-windows\clicalc.exe --no-state "sin(360)"
```

The `.exe` alone is not a standalone distribution; it needs those runtime DLLs.
The **Windows** GitHub Actions workflow builds and tests this configuration on
pushes and pull requests. Its first successful run is the Windows validation
for these instructions; local macOS tests do not verify Windows behavior.

## Tutorial

Start the calculator with `./build/clicalc`. The `>` characters below are prompts;
type only the text after them. Each example uses the state from the previous one.
The live preview is omitted from the transcripts for clarity.

### 1. Calculate and reuse an answer

Multiplication takes precedence over addition. `ans` holds the last result;
starting with `/`, `*`, or `+` inserts it automatically.

```text
> 4+5*4
ans = 24
> /2
ans = 12
> --2
ans = 10
> -3
ans = -3
> ans^2
ans = 9
```

Use **`--` to subtract from `ans`**, and **a single `-` for a new negative number**.
The terminal visibly expands `--2` to `ans -2` while you type.

### 2. Store values and define a function

Assign a variable with `=`. Define a function by naming its parameters:

```text
> resistance = 4700
resistance = 4700
> par(x,y) = x*y/(x+y)
par(x,y) = x*y/(x+y)
> par(resistance,10000)
ans = 3197.27891156462585034013605442
> list
par(x,y) = x*y/(x+y)
> disp resistance
resistance = 4700
```

`vlist` lists variables, including the built-in `ans`, `pi`, and `e`. `list`
lists custom functions. Remove one with `del resistance` or `del par`.

### 3. Change the display base

```text
> 255
ans = 255
> base hex
ans = 0xFF
> base bin
ans = 0b11111111
> base dec
ans = 255
> vlist
ans = 255
e = 2.71828182845904523536028747135
pi = 3.14159265358979323846264338328
resistance = 4700
```

Base changes affect **output only**: `10` always means decimal ten. Use prefixes
to enter another base, such as `0xFF` or `0b1111`. `bases` shows all three bases
without changing the selected display base.

### 4. Use scientific math and engineering format

```text
> mode deg
> sin(30)
ans = 0.5
> format eng
> 12345
ans = 12.345k
> 0.000012345
ans = 12.345u
> format auto
```

`mode rad` switches to radians. `sigfigs 100` displays up to 100 significant digits;
`sigfigs 30` restores the default. `format` without an argument shows the current
format. Engineering format uses SI prefixes and never scientific notation.

### 5. Convert units

```text
> 70 mi/h -> m/s
ans = 31.2928
> 32 degF -> degC
ans = 0
> unit furlong = 201.168 m
Defined unit furlong.
> 1 furlong -> ft
ans = 660
```

Put a space between the quantity and its units. Use `units` to browse available
names or `units temperature` to filter by dimension.

### 6. Edit, get help, and save a session

| Key or command | Action |
| --- | --- |
| Up / Down | Browse command history |
| Ctrl-R | Search history; Ctrl-E accepts a match for editing |
| Tab | Complete a command, variable, function, or unit |
| Ctrl-L or `cls` | Clear the screen; Ctrl-L retains the expression being edited |
| `preview off` / `preview on` | Disable / enable the bottom preview line |
| `help` | Show calculator commands and functions |
| Ctrl-D at an empty prompt, or `exit` | End the session |

History and custom definitions persist by default. To also save variables and
preferences, start the calculator with `./build/clicalc --restore-session`.
Use `--no-state` for a session that does not read or write persistent state.

### 7. Run calculations from the shell

Quote expressions so the shell does not interpret operators:

```sh
./build/clicalc '2^10 + sqrt(16)'
./build/clicalc -e 'x=12;' -e 'x*3'
./build/clicalc -f examples/tour.calc
printf 'x=12;\nx*3\n' | ./build/clicalc
```

A semicolon suppresses the preceding statement's output. `#` starts a comment.
Scripts stop at the first error and return exit status 1.

## Command-line help

Use `./build/clicalc --help` for launch options and `help` inside the calculator
for expression syntax and commands.

```text
Usage: clicalc [options] [expression]
  -e, --eval EXPR     Evaluate an expression or command (repeatable)
  -f, --file FILE     Run a script; '-' reads standard input
  --base bin|dec|hex  Select display base
  --precision N      Display 1..100 significant digits
  --state FILE       Load/save function and unit definitions at FILE
  --restore-session  Restore/save variables and settings as well as definitions
  --session FILE     Use a full session snapshot at FILE
  --history FILE     Use a persistent history file (interactive only)
  --no-history       Disable persistent command history
  --no-preview       Disable live result previews
  --no-state         Disable all persistent state and history
  -h, --help         Show this help
  --version          Show version
```

Without an expression or file, the program starts an interactive terminal
session or reads piped input. Put `--` before an expression that resembles an
option, for example `./build/clicalc -- '-sqrt(4)'`.

## Expressions

- Arithmetic: `+ - * /`, exponentiation `^`, integer remainder `%`, postfix factorial `!`.
- Power is right associative: `2^3^2` is `512`. `-2^2` is `-4`; `(-2)^2` is `4`.
- Grouping: both parentheses and square brackets.
- Bitwise: `& | @` (AND, OR, XOR), `~`, `<< >>`. These require integers.
  Negative bitwise operands follow GMP's signed, infinite two's-complement
  semantics; right shifts round toward negative infinity.
- Comparisons: `< <= > >= == !=`; logical `&& || !` produce `0` or `1`.
  Logical AND/OR short circuit. Comparisons are binary, so write
  `1 < x && x < 10`, rather than a chained comparison.
- Numbers: decimal, scientific (`1.2e-6`), hex (`0xFF`), binary (`0b101`),
  including fractional hex/binary (`0xA.F`, `0b101.11`).
- SI suffixes: `Q R Y Z E P T G M k c m u n p f a z y r q`, e.g. `5M+100k`.
  A trailing `E` means exa; `E` followed by an exponent (e.g. `1E3`) still means
  scientific notation.
- Variables: `x=25`. `ans` tracks the previous result; `pi` and `e` are constants.
  These three names are protected. Variable/function names are case sensitive.
- Functions: `par(x,y)=x*y/(x+y)`. Parameters are local; other variables are
  resolved from the session when called. Definitions may reference other functions.
- The terminal editor expands an initial `+`, `*`, or `/` to `ans ` followed
  by that operator; `--` expands to `ans -`. A single `-` starts a negative
  expression. Operators inside an expression are entered normally.
  Scripts and the minimal frontend retain the original shorthand: initial `+`,
  `*`, `/`, or `--` operates on `ans`, while a single `-` starts a negative number.
- Separate statements with `;`. Each semicolon suppresses the preceding
  statement's output. `#` starts a comment, including after an expression.

Built-in function names are case insensitive:

```text
sqrt ln log exp log2 log8 logN   (log is base 10; N is an integer > 1)
sin cos tan asin acos atan sinh cosh tanh
abs round ceil floor trunc
rand(max) mod(x,y) min(x,y) max(x,y) pow(x,y) atan2(y,x)
```

`round` rounds halfway cases away from zero. `%` requires integers; `mod` also
accepts fractions. `rand(max)` returns a value in `[0,max)` with 64 random bits.
Trigonometry defaults to degrees; `mode rad` switches trigonometric inputs and
inverse-trigonometric outputs to radians, and `mode deg` switches back to degrees.
Restoring a saved session preserves its angle mode. Hyperbolic functions are unaffected.
In degree mode, exact quarter turns return exact sine/cosine values:
`sin(360)` is `0`, `cos(360)` is `1`, and `sin(90)` is `1`.
Tangent at odd multiples of 90 degrees reports a domain error.
Use parentheses (`sin(360)`, not `sin360`). Enter `mode` to check the current angle units.

## Commands

| Command | Behavior |
| --- | --- |
| `base bin`, `base dec`, `base hex` | Change output base and redisplay `ans` |
| `base 2`, `base 10`, `base 16` | Numeric aliases |
| `base` | Show current output base |
| `bases [expression]` | Show decimal, hexadecimal, and binary together; defaults to `ans` |
| `programmer 8/16/32/64 [signed/unsigned]` | Choose one word width and signedness |
| `programmer off` | Return to high-precision real arithmetic |
| `programmer` | Show current programmer settings |
| `preview on`, `preview off` | Enable/disable the live terminal preview |
| `display hex` | Compatibility alias for `base` |
| `vlist` | List all variables, including `ans`, `pi`, and `e` |
| `list` | List user-defined functions |
| `disp name` | Show one variable |
| `del name`, `rem name` | Delete a user variable or function |
| `del all` | Clear user variables/functions, retaining constants and units |
| `mode rad`, `mode deg` | Set trigonometric angle units |
| `sigfigs N` | Set 1–100 displayed significant digits (default 30) |
| `format auto/never/always/eng/prefix/finance` | Select one of these display modes |
| `unit name = scale units` | Define a new unit, e.g. `unit furlong = 201.168 m` |
| `units [filter]` | Discover units by name or dimension, e.g. `units temperature` |
| `echo text` | Print text |
| `clear`, `cls` | Clear an interactive terminal |
| `help` | Show the command/function reference |
| `exit`, `quit` | End the session or script |

**Base selection only changes output.** Unprefixed numbers always mean decimal.
`base bin/dec/hex` is the command equivalent of the reference application's
Ctrl-1/2/3 shortcuts; raw Ctrl-number terminal keybindings are not implemented.
Negative base output uses a minus sign, and fractions are preserved.
In programmer mode, hexadecimal and binary instead show padded two's-complement
bit patterns at the selected width.

Scientific display modes: `auto` chooses compact notation, `never` uses fixed
notation, `always` uses scientific notation, and `eng` uses SI prefixes:
`12345` becomes `12.345k` and `0.000012345` becomes `12.345u`. It never prints
scientific notation; values outside the supported prefix range use ordinary
decimal notation (subject to the existing large-output limit). Prefixes range
from quecto (`q`, 10^-30) through quetta (`Q`, 10^30), using `u` for micro.
See the [BIPM SI prefix table](https://www.bipm.org/en/measurement-units/si-prefixes).
`prefix` is a synonym for the same SI-style output, and `finance` uses `sigfigs`
**decimal places**. Enter one mode name, e.g. `format eng`. `format` replaces
the previous `scimode` command; saved format preferences remain compatible.

## Programmer mode

```text
> programmer 8 signed
programmer = 8 signed
> 127+1
ans = -128
> bases
dec = -128
hex = 0x80
bin = 0b10000000
> programmer 8 unsigned
programmer = 8 unsigned
> 255+1
ans = 0
> programmer off
programmer = off
```

`programmer 8 signed`, `programmer 16 unsigned`, etc. enable integer arithmetic.
Every integer operand/result wraps modulo 2^width, with signed interpretation
when selected. Division truncates toward zero; signed right shifts extend the
sign bit, and unsigned right shifts insert zeros. Shift counts must be
nonnegative and smaller than the word width. Fractions are rejected in programmer
expressions. Powers and factorials retain correct low bits even when their full
result would be very large. Decimal word output is exact regardless of `sigfigs`.
Stored variables are interpreted under the active width when used; changing
mode alone does not destructively rewrite them. `bases` is a display-only query
and does not change `ans` or the default base. Scientific mode remains the default.

## Unit conversion

```text
10 in -> cm
20*5 in -> cm
in -> cm
70 mi/h -> m/s
50 N-m -> millijoules
10000 m^2 -> acres
unit furlong = 201.168 meters
1 furlong -> ft
```

Separate the quantity expression from its units with whitespace. Unit products
accept `*` or `-`; quotients use `/`, powers use `^`, and parentheses group units.
Multiplication and division are left associative: use `kg/(m*s^2)` when the
whole product belongs in the denominator. Units must have matching dimensions.

Includes SI base and derived units, SI prefixes, and common length, area,
volume, mass, time, pressure, energy, and angle units. Names and symbols are
case sensitive (`m` is meters, `M` is the mega prefix). Common plural names work.
US customary volume measures, the international foot, and a Julian year are
used. Temperature conversion includes offsets:

```text
0 degC -> degF          # 32
32 degF -> degC         # 0
273.15 K -> degC        # 0
100 Celsius -> Fahrenheit  # 212
9 deltaF -> deltaC      # 5 (temperature differences)
units temperature
```

Use `degC`/`celsius`/`Celsius` and `degF`/`fahrenheit`/`Fahrenheit`. Existing `C`
and `F` still mean coulombs and farads. Offset temperatures require an explicit
quantity and cannot be multiplied, divided, prefixed, exponentiated, or used as
the scale for a custom unit. Use `deltaC` and `deltaF` for differences or rates
such as `deltaC/s`. Unit discovery returns names, dimensions, scales, and offsets
through the engine API; the terminal shows names and dimensions.

## Precision and errors

The core uses MPFR with **384 binary bits**, about 115 decimal digits of working
precision, and round-to-nearest arithmetic. Display precision is adjustable up
to 100 significant digits. This is a real-number floating-point calculator,
not symbolic algebra or an arbitrary-length integer engine: values beyond 384
significant binary bits are rounded. Decimal fractions may not have exact binary
representations, so hex/binary output can expose the rounded binary expansion.
Integers are also subject to the selected decimal display precision.

An invalid line reports an error and leaves the session unchanged. Batch input
stops at the first failed line and returns exit status 1. An interactive session
continues so the input can be corrected. Scripts report the file and line number.
Errors underline the offending token or mark a missing argument at the end:

```text
error: Expected a number, variable, or '('
  12 + sqrt(
            ^
```

Diagnostics include a stable error category, a zero-based byte span in the
submitted line, and function-call context when applicable.
Input length, parser nesting, recursion, factorials, shifts, and large fixed/base
output have explicit limits to prevent accidental runaway work.

## Persistence and scripts

Interactive sessions save custom functions and units at exit to
`$XDG_CONFIG_HOME/clicalc/definitions.calc`, or
`$HOME/.config/clicalc/definitions.calc` when XDG_CONFIG_HOME is unset.
On Windows, when XDG_CONFIG_HOME is unset, files are saved under
`%APPDATA%\clicalc` (with the HOME location as a fallback).
The most recent 1,000 history entries are stored alongside definitions in
`history`, after each submitted command. Use `--history FILE` for a different
location or `--no-history` to keep only in-memory history. Readline's Ctrl-R
searches loaded and current commands. The minimal frontend does not persist history.

Full session restore is **opt-in**:

```sh
./build/clicalc --restore-session       # default session.calc beside definitions
./build/clicalc --session .local/work.calc  # named session; also works with -e/-f
```

A full snapshot saves variables (including `ans`), function/unit definitions,
base, angle mode, precision, scientific notation, word width/signedness, and
preview preference at normal exit. Variables retain their full working precision.
Without these flags, variables and settings start fresh. An existing full
snapshot takes precedence over the separate definitions file. Command-line
`--base`, `--precision`, and `--no-preview` override restored settings. The random
generator starts a fresh sequence for each process and is not serialized.

Batch invocations do not load or save state by default. Pass `--state FILE` to
explicitly enable saved definitions, or `--session FILE` for a full snapshot.
`--no-state` disables all disk persistence, including history. Definition and
session files are validated before loading and replaced atomically on successful
writes. A malformed snapshot leaves existing state untouched. Separate sessions
sharing a snapshot use the last saved version; concurrent snapshot merging is
not implemented. History appends retain submitted entries; truncation to 1,000
entries is best-effort when multiple processes share a history file.

Use repeated `-e` and `-f` options to run expressions and scripts in order within
one session. `-f -` reads standard input. Quote expressions to prevent shell
expansion; use `--` before expressions that resemble command flags.

## Engine architecture and Apple apps

`include/clicalc/engine.hpp` exposes `Engine::process`, `Engine::preview`,
`Engine::complete`, and structured session output. `clicalc_core` / `build/libclicalc.a` contains parsing,
math, units, variables, and formatting. It has no terminal, filesystem, or UI
dependencies. `src/main.cpp` and `src/terminal.cpp` handle input, rendering,
scripts, and files.

```cpp
#include <clicalc/engine.hpp>

clicalc::Engine engine;
auto result = engine.process("voltage=12; voltage/4700");
if (result.error) {
    auto code = result.error->code;       // ErrorCode enum
    auto span = result.error->span;       // optional {offset, length}
    // Render the message/span using the native UI.
} else {
    for (const auto& item : result.items) {
        if (item.suppressed) continue;
        // item.kind, name, value, formatted, function, unit, and base
        // are available independently of the compatibility text output.
    }
}
auto tentative = engine.preview("voltage=24; voltage/4700"); // no state changes
auto suggestions = engine.complete("sq", 2); // replacement span + typed candidates
auto temperatures = engine.units("temperature");
auto snapshot = engine.session();
engine.restore_session(snapshot);         // validated, atomic restore
```

`process()` returns calculation failures as `Result::error`; `items` describes
numeric values, variables, function definitions, units, settings, and messages.
Suppressed statements remain available as structured records with `suppressed=true`.
`settings` contains a complete presentation/programmer preference snapshot.
Numeric items carry the full-precision `Number` and a separate `formatted` value;
function/unit items carry parameters/expressions or scales/offsets/dimensions.
`exit` and `clear` are explicit UI actions. `preview()` never emits these actions.
The older `execute()`/`evaluate()` APIs throw `CalcError` with the same diagnostic
for calculation failures, and `Result::output` remains available for simple text
clients. Error spans use UTF-8 byte offsets; clients can translate these to their
native string indices. A span of length zero identifies a missing token at EOF.

Future macOS/iOS frontends can own an engine per calculator session and call it
through an Objective-C++ wrapper or Swift C++ interop. With CMake, set
`-DCLICALC_BUILD_CLI=OFF -DBUILD_TESTING=OFF` to build only the core. Apple builds
will need MPFR/GMP built for the selected device/simulator targets. No Apple UI
or packaging is included yet. Keep each engine confined to one thread or serialize
access to it.

The initial release covers the terminal workflow, not every preference in the
reference GUI. Remaining features include locale/thousands separators,
ten-key mode, clipboard integration,
and native Apple interfaces.

## Development

The repository contains source, tests, and examples; generated binaries and
local calculator sessions are ignored by Git.

```text
include/clicalc/       Public C++ engine and diagnostic interfaces
src/                  Engine, parser, math, units, session, and terminal code
tests/                Engine, CLI, and pseudo-terminal regression tests
examples/tour.calc    Runnable feature tour
CMakeLists.txt        CMake build configuration
Makefile              Make build configuration
LICENSE               MIT license for this project's source
```

Build and test before submitting a change:

```sh
make -j4
make test
```

`make test` runs the engine and feature checks, command-line integration tests,
and (with Readline enabled) interactive terminal tests. The latter use Python's
POSIX pseudo-terminal support on Linux/macOS and cover editing, history,
completion, previews, screen clearing, and terminal resizing. No external Python
packages are required. CMake users can run the same suites with `ctest` as shown
in the build instructions.

Keep engine changes independent of terminal I/O so the same C++ library can
support future macOS and iOS interfaces. Include a regression test when fixing a
behavioral bug, and update the command reference when changing user-facing syntax.
Repository text uses UTF-8 and LF endings; `.editorconfig` records indentation
settings and `.gitattributes` keeps line endings consistent.

Store repository-local history and snapshots under `.local/` to keep them out of
version control. The default persistent files live outside the checkout, as
described in [Persistence and scripts](#persistence-and-scripts).

## License

This project's source is licensed under the [MIT License](LICENSE).
Copyright (c) 2026 Hibouonics.

MPFR, GMP, and optional GNU Readline remain under their respective upstream
licenses; the MIT license does not replace those dependency licenses.

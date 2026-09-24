"""Small VT screen model for asserting calculator/Readline screen geometry.

Implements the cursor, erase, insert/delete, wrap, and scroll-region sequences
used by the CLI; styling and bracketed-paste modes do not affect these checks.
"""
import codecs


class Screen:
    def __init__(self, rows, columns):
        self.rows, self.columns = rows, columns
        self.cells = [[" "] * columns for _ in range(rows)]
        self.row = self.column = 0
        self.top, self.bottom = 0, rows - 1
        self.saved = (0, 0, False)
        self.wrap = False
        self.scrolls = 0
        self.pending = ""
        self.decoder = codecs.getincrementaldecoder("utf-8")("replace")

    @property
    def lines(self):
        return ["".join(row).rstrip() for row in self.cells]

    def resize(self, rows, columns):
        self.cells = [(row[:columns] + [" "] * columns)[:columns] for row in self.cells[:rows]]
        self.cells += [[" "] * columns for _ in range(rows - len(self.cells))]
        self.rows, self.columns = rows, columns
        self.row, self.column = min(self.row, rows - 1), min(self.column, columns - 1)
        self.top, self.bottom = 0, rows - 1
        self.wrap = False

    def index(self):
        if self.row == self.bottom:
            self.cells.pop(self.top)
            self.cells.insert(self.bottom, [" "] * self.columns)
            self.scrolls += 1
        else:
            self.row = min(self.rows - 1, self.row + 1)
        self.wrap = False

    def csi(self, args, command):
        private = args.startswith("?")
        args = args.lstrip("?")
        values = [int(v) if v.isdigit() else 0 for v in args.split(";")]
        n = values[0] or 1
        if private or command in "mhlnct":
            return
        self.wrap = False
        if command == "A": self.row = max(self.top, self.row - n)
        elif command == "B": self.row = min(self.bottom, self.row + n)
        elif command == "C": self.column = min(self.columns - 1, self.column + n)
        elif command == "D": self.column = max(0, self.column - n)
        elif command == "G": self.column = min(self.columns - 1, n - 1)
        elif command in "Hf":
            self.row = min(self.rows - 1, n - 1)
            self.column = min(self.columns - 1, (values[1] or 1) - 1) if len(values) > 1 else 0
        elif command == "r":
            self.top = n - 1
            self.bottom = (values[1] or self.rows) - 1 if len(values) > 1 else self.rows - 1
            self.row = self.column = 0
        elif command == "K":
            begin = 0 if values[0] in (1, 2) else self.column
            end = self.column + 1 if values[0] == 1 else self.columns
            self.cells[self.row][begin:end] = [" "] * (end - begin)
        elif command == "J":
            if values[0] == 2:
                self.cells = [[" "] * self.columns for _ in range(self.rows)]
            elif values[0] == 0:
                self.cells[self.row][self.column:] = [" "] * (self.columns - self.column)
                for row in range(self.row + 1, self.rows): self.cells[row] = [" "] * self.columns
        elif command == "@":
            row = self.cells[self.row]
            self.cells[self.row] = (row[:self.column] + [" "] * n + row[self.column:])[:self.columns]
        elif command == "P":
            row = self.cells[self.row]
            self.cells[self.row] = (row[:self.column] + row[self.column + n:] + [" "] * n)[:self.columns]
        else:
            raise AssertionError(f"Unmodeled VT sequence: CSI {args}{command}")

    def feed(self, data):
        self.pending += self.decoder.decode(data)
        while self.pending:
            c = self.pending[0]
            if c == "\x1b":
                if len(self.pending) < 2: return
                next_char = self.pending[1]
                if next_char == "[":
                    end = 2
                    while end < len(self.pending) and not ("@" <= self.pending[end] <= "~"): end += 1
                    if end == len(self.pending): return
                    self.csi(self.pending[2:end], self.pending[end])
                    self.pending = self.pending[end + 1:]
                    continue
                if next_char == "7": self.saved = (self.row, self.column, self.wrap)
                elif next_char == "8": self.row, self.column, self.wrap = self.saved
                elif next_char == "D": self.index()
                elif next_char == "M": self.row = max(self.top, self.row - 1)
                elif next_char in "()=>":
                    if next_char in "()":
                        if len(self.pending) < 3: return
                        self.pending = self.pending[1:]
                else: raise AssertionError(f"Unmodeled VT escape: {next_char!r}")
                self.pending = self.pending[2:]
                continue
            self.pending = self.pending[1:]
            if c == "\r": self.column, self.wrap = 0, False
            elif c == "\n": self.index()
            elif c == "\b": self.column, self.wrap = max(0, self.column - 1), False
            elif c == "\t": self.column = min(self.columns - 1, (self.column // 8 + 1) * 8)
            elif c >= " ":
                if self.wrap: self.column = 0; self.index()
                self.cells[self.row][self.column] = c
                if self.column == self.columns - 1: self.wrap = True
                else: self.column += 1

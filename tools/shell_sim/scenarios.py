#!/usr/bin/env python3
"""Generate shell_sim key scripts that walk every operation of a program.

    tools/shell_sim/scenarios.py <program> > script.txt

The scripts enter a fixed set of matrices, then run each operation the
program's menus reach, snapshotting the result screen, every step, and a few
error messages. Running the same script before and after a change and
comparing the snapshots shows whether the UI changed.

Menu positions are spelled out per program below: they mirror the program's
menu layout, so a layout change shows up here as well as on screen.
"""
import sys

# slot -> (rows, cols, row major values)
MATRICES = {
    "A": (3, 3, [2, 1, 1, 4, -6, 0, -2, 7, 2]),  # invertible
    "B": (3, 1, [5, -2, 9]),  # right hand side for A
    "C": (3, 5, [-3, 6, -1, 1, -7, 1, -2, 2, 3, -1, 2, -4, 5, 8, -4]),  # Lay 4.2 ex 3
    "D": (2, 3, [1, 2, 3, 4, 5, 6]),
    "E": (3, 3, [0, 1, 2, 1, 0, 3, 4, -3, 8]),  # LU needs a row swap
    "F": (3, 1, [1, 2, 3]),
    "G": (3, 1, [4, 5, 6]),
    "H": (3, 3, [1, 2, 3, 2, 4, 6, 1, 0, 1]),  # singular
}
SLOTS = "ABCDEFGH"
STEP_PAGES = 24  # right presses per steps viewer; extra presses stay on the last step


class Script:
    def __init__(self):
        self.lines = []

    def __call__(self, line):
        self.lines.append(line)

    def press(self, key, n=1):
        if n > 0:
            self(f"press {key} {n}")

    def text(self):
        return "\n".join(self.lines) + "\n"


def enter_matrices(s, main_index_of_matrices=0):
    """From the main menu (cursor on Matrices), fill every slot, return to main."""
    s.press("down", main_index_of_matrices)
    s.press("enter")  # Matrices
    s.press("enter")  # Edit slots
    s("snap slots_empty")
    cur = 0
    for slot in SLOTS:
        rows, cols, vals = MATRICES[slot]
        idx = SLOTS.index(slot)
        s.press("down", idx - cur)
        cur = idx
        s.press("enter")  # unset slot -> size page, 2x2
        s.press("right", rows - 2)
        s.press("left", 2 - rows)
        s.press("up", cols - 2)
        s.press("down", 2 - cols)
        s(f"snap dim_{slot}")
        s.press("enter")
        for v in vals:
            s(f"type {v}")
            s.press("enter")
        s(f"snap editor_{slot}")
        s.press("clear")  # editor -> slot list
    s("snap slots_full")
    s.press("clear")  # slot list -> Matrices
    s.press("clear")  # Matrices -> main
    s.press("up", main_index_of_matrices)


def pick(s, slot, cur=0):
    """On a slot picker whose cursor is at `cur`, choose `slot`."""
    idx = SLOTS.index(slot)
    s.press("down", idx - cur)
    s.press("up", cur - idx)
    s.press("enter")
    return idx


def steps(s, name, pages=STEP_PAGES):
    s.press("2nd")
    s(f"snap {name}_step00")
    for i in range(1, pages + 1):
        s.press("right")
        s(f"snap {name}_step{i:02d}")
    s.press("clear")  # steps -> result


def run(s, name, path, slots, has_steps=True, ops_main_index=1, extra=None, pages=STEP_PAGES, error=False):
    """Main menu -> Operations (or a direct entry) -> path of menu indices -> pick slots.

    `path` is the list of cursor positions to choose, starting in the menu
    that main's entry opens. The result is snapshotted, then the steps.
    """
    s.press("down", ops_main_index)
    s.press("enter")
    for idx in path:
        s.press("down", idx)
        s.press("enter")
    s(f"snap {name}_pick")
    cur = 0
    for slot in slots:
        cur = pick(s, slot, cur)
    s(f"snap {name}_result")
    if error:
        s("wait 100")  # let the message expire
        if len(slots) == 2:
            s.press("clear")  # second pick -> first pick
        s.press("clear")  # slot picker -> menu
    else:
        if extra:
            extra(s)
        if has_steps:
            steps(s, name, pages)
        s.press("clear")  # result -> menu
    # one menu page per entry chosen on the way to the operation: the last
    # choice opened the slot picker, which the result replaced
    for _ in path:
        s.press("clear")
    s.press("up", ops_main_index)


def named_views(name, n):
    def f(s):
        for i in range(1, n):
            s.press("right")
            s(f"snap {name}_view{i}")
        s.press("right")  # back to the first view
    return f


def matlite(s):
    # Operations: 0 Add/Sub, 1 Multiply, 2 Det, 3 REF/RREF, 4 Solve, 5 Span/Indep,
    #             6 Invertible?, 7 LU Factorization, 8 LU Solve
    s("snap main")
    enter_matrices(s)
    s.press("down")
    s.press("enter")
    s("snap operations")
    for i, sub in [(0, "addsub"), (1, "multiply"), (3, "refrref"), (5, "span")]:
        s.press("down", i)
        s.press("enter")
        s(f"snap menu_{sub}")
        s.press("clear")
        s.press("up", i)
    s.press("clear")
    s.press("up")
    run(s, "add", [0, 0], "AA")
    run(s, "sub", [0, 1], "AH")
    run(s, "add_mismatch", [0, 0], "AD", error=True)
    run(s, "mul", [1, 0], "DA")
    run(s, "mulcolrow", [1, 1], "DA")
    run(s, "det", [2], "A")
    run(s, "det_nonsquare", [2], "D", error=True)
    run(s, "ref", [3, 0], "C")
    run(s, "rref", [3, 1], "C")
    run(s, "solve", [4], "AB")
    run(s, "solve_param", [4], "CB", has_steps=True)
    run(s, "inspan", [5, 0], "AB")
    run(s, "spantest", [5, 1], "C")
    run(s, "indep", [5, 2], "C")
    run(s, "onto", [5, 3], "D")
    run(s, "invertible", [6], "A")
    run(s, "invertible_singular", [6], "H")
    run(s, "invertible_nonsquare", [6], "D", has_steps=False)
    run(s, "lu", [7], "A", extra=named_views("lu", 2))
    lu_swap(s, accept=False)
    lu_swap(s, accept=True)
    run(s, "lusolve", [8], "AB", extra=named_views("lusolve", 1))


def lu_swap(s, accept):
    name = "lu_swap_yes" if accept else "lu_swap_no"
    s.press("down")
    s.press("enter")
    s.press("down", 7)
    s.press("enter")
    pick(s, "E")
    s(f"snap {name}_confirm")
    if accept:
        s.press("enter")
        s(f"snap {name}_result")
        for i in range(1, 3):
            s.press("right")
            s(f"snap {name}_view{i}")
        steps(s, name)
        s.press("clear")  # result -> operations
    else:
        s.press("clear")  # confirm -> slot picker
        s(f"snap {name}_back")
        s.press("clear")  # slot picker -> operations
    s.press("clear")
    s.press("up")


def matbasis(s, legacy=False):
    # Operations: 0 Col(A) & Nul(A)
    # `legacy` is the layout before programs shared one menu tree, where the
    # operation sat on the main menu itself.
    s("snap main")
    enter_matrices(s)
    if not legacy:
        s.press("down")
        s.press("enter")
        s("snap operations")
        s.press("clear")
        s.press("up")
    for name, slot in [("basis_lay", "C"), ("basis_inv", "A"), ("basis_sing", "H"), ("basis_wide", "D")]:
        s.press("down")
        s.press("enter")
        if not legacy:
            s.press("enter")  # Operations -> Col(A) & Nul(A)
        s(f"snap {name}_pick")
        pick(s, slot)
        s(f"snap {name}_result")
        for i in range(1, 3):
            s.press("right")
            s(f"snap {name}_view{i}")
        s.press("right")
        steps(s, name, pages=16)
        s.press("clear")  # result -> menu
        if not legacy:
            s.press("clear")  # Operations -> main
        s.press("up")


def matrix_full(s, legacy=False):
    # Operations: 0 Add/Sub, 1 Multiply, 2 Dot, 3 Cross, 4 Det, 5 Cofactor/Cramer,
    #             6 REF/RREF, 7 Solve, 8 Span/Indep, 9 Transpose, 10 Inverse,
    #             11 Invertible?, 12 LU Factorization, 13 LU Solve, 14 Spaces,
    #             15 Col(A) & Nul(A), 16 Projection
    # `legacy` is the layout before Cofactor/Cramer had a menu entry.
    def at(n):  # index in the current layout of what the legacy layout had at n
        return n if legacy or n < 5 else n + 1

    s("snap main")
    enter_matrices(s)
    s.press("down")
    s.press("enter")
    s("snap operations")
    s.press("down", at(15))
    s("snap operations_bottom")
    s.press("clear")
    s.press("up")
    for i, sub in [(at(7), "span"), (at(13), "spaces")]:
        s.press("down")
        s.press("enter")
        s.press("down", i)
        s.press("enter")
        s(f"snap menu_{sub}")
        s.press("clear")
        s.press("clear")
        s.press("up")
    run(s, "dot", [2], "FG")
    run(s, "cross", [3], "FG")
    run(s, "cross_bad", [3], "DG", error=True)
    run(s, "det", [4], "A")
    run(s, "basis_from_set", [at(7), 3], "C")
    run(s, "transpose", [at(8)], "D")
    run(s, "inverse", [at(9)], "A")
    run(s, "inverse_singular", [at(9)], "H", error=True)
    run(s, "colspace", [at(13), 0], "C")
    run(s, "rowspace", [at(13), 1], "C")
    run(s, "nullspace", [at(13), 2], "C")
    run(s, "leftnull", [at(13), 3], "C")
    run(s, "basis", [at(14)], "C", extra=named_views("basis", 3), pages=16)
    run(s, "projection", [at(15)], "FG", extra=named_views("projection", 2))
    if legacy:
        return

    # Cofactor/Cramer: 0 Cofactor, 1 Minor Matrix, 2 Cramer
    s.press("down")
    s.press("enter")
    s.press("down", 5)
    s.press("enter")
    s("snap menu_cofactor_cramer")
    s.press("clear")
    s.press("clear")
    s.press("up")
    run(s, "minor", [5, 1], "A", has_steps=False)  # no explanation for the matrix of minors
    run(s, "minor_nonsquare", [5, 1], "D", error=True)
    run(s, "cramer", [5, 2], "AB", has_steps=False, extra=cramer_steps)
    run(s, "cramer_singular", [5, 2], "HB", error=True)
    # cofactor: pick A, choose C_{2,3} on the index page, run
    s.press("down")
    s.press("enter")
    s.press("down", 5)
    s.press("enter")
    s.press("enter")  # Cofactor
    pick(s, "A")
    s("snap cofactor_index")
    s.press("right")  # i = 2
    s.press("down")
    s.press("right", 2)  # j = 3
    s("snap cofactor_index_set")
    s.press("enter")
    s("snap cofactor_result")
    steps(s, "cofactor")
    s.press("clear")  # result -> Cofactor/Cramer
    s.press("clear")
    s.press("clear")
    s.press("up")


def cramer_steps(s):
    # 2ND opens the menu of determinants: det A, then each A_i
    s.press("2nd")
    s("snap cramer_steps_menu")
    for k in range(4):
        s.press("enter")
        s(f"snap cramer_det{k}_step00")
        for i in range(1, 8):
            s.press("right")
            s(f"snap cramer_det{k}_step{i:02d}")
        s.press("clear")  # steps -> menu
        s.press("down")
    s.press("clear")  # menu -> result


PROGRAMS = {
    "matlite": matlite,
    "matbasis": matbasis,
    "matbasis-legacy": lambda s: matbasis(s, legacy=True),
    "matrix": matrix_full,
    "matrix-legacy": lambda s: matrix_full(s, legacy=True),
}

if __name__ == "__main__":
    if len(sys.argv) != 2 or sys.argv[1] not in PROGRAMS:
        sys.exit(f"usage: {sys.argv[0]} {{{','.join(PROGRAMS)}}}")
    s = Script()
    PROGRAMS[sys.argv[1]](s)
    s("quit")
    sys.stdout.write(s.text())

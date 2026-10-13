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


def enter_matrices(s, main_index_of_matrices=0, matrices=MATRICES):
    """From the main menu (cursor on Matrices), fill every slot, return to main."""
    s.press("down", main_index_of_matrices)
    s.press("enter")  # Matrices
    s.press("enter")  # Edit slots
    s("snap slots_empty")
    cur = 0
    for slot in SLOTS:
        rows, cols, vals = matrices[slot]
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


def matbasis(s, legacy=False, op_index=0, enter=True):
    # Operations: 0 Col(A) & Nul(A)
    # `legacy` is the layout before programs shared one menu tree, where the
    # operation sat on the main menu itself. `op_index` is where the operation
    # sits in a program with more in its Operations menu, and `enter` False
    # reuses the matrices already entered.
    if enter:
        s("snap main")
        enter_matrices(s)
    if not legacy and enter:
        s.press("down")
        s.press("enter")
        s("snap operations")
        s.press("clear")
        s.press("up")
    for name, slot in [("basis_lay", "C"), ("basis_inv", "A"), ("basis_sing", "H"), ("basis_wide", "D")]:
        s.press("down")
        s.press("enter")
        if not legacy:
            s.press("down", op_index)
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
    # Operations: 0 Add/Sub, 1 Multiply, 2 Dot, 3 Cross, 4 Det,
    #             5 Cofactor expansion, 6 Det (triangular), 7 Adjoint adj(A),
    #             8 Cofactor/Cramer, 9 Area, 10 REF/RREF, 11 Solve, 12 Span/Indep,
    #             13 Transpose, 14 Inverse, 15 Invertible?, 16 Indep.? (det),
    #             17 LU Factorization, 18 LU Solve, 19 Spaces, 20 Col(A) & Nul(A),
    #             21 Projection
    # `legacy` is the layout before Cofactor/Cramer had a menu entry.
    def cur(m):  # index now of what the layout before the determinant ops had at m
        i = m if m < 5 else (m + 3 if m < 12 else m + 4)
        return i + 1 if i >= 9 else i  # Area sits after Cofactor/Cramer

    def at(n):  # index in the current layout of what the legacy layout had at n
        return n if legacy else cur(n if n < 5 else n + 1)

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
    s.press("down", cur(5))
    s.press("enter")
    s("snap menu_cofactor_cramer")
    s.press("clear")
    s.press("clear")
    s.press("up")
    run(s, "minor", [cur(5), 1], "A", has_steps=False)  # no explanation for the matrix of minors
    run(s, "minor_nonsquare", [cur(5), 1], "D", error=True)
    run(s, "cramer", [cur(5), 2], "AB", has_steps=False, extra=cramer_steps)
    run(s, "cramer_singular", [cur(5), 2], "HB", error=True)
    # cofactor: pick A, choose C_{2,3} on the index page, run
    s.press("down")
    s.press("enter")
    s.press("down", cur(5))
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


# the Math 210 determinants worksheet
WORKSHEET_DET = {
    "A": (3, 3, [2, 5, -3, 4, 7, -1, 1, 0, 3]),  # 1(a)
    "B": (4, 4, [4, -5, 2, 6, 0, 1, 4, 6, 0, -2, 2, 6, 0, 4, -3, 2]),  # 1(b)
    "C": (3, 3, [2, 0, 0, 4, 7, 0, 1, -2, 3]),  # 2(a) lower triangular
    "D": (4, 4, [-2, 3, 8, 1, 0, 1, 2, -5, 0, 0, -1, 4, 0, 0, 0, 2]),  # 2(b) upper triangular
    "E": (3, 3, [4, -2, 8, 8, -8, 16, 4, -4, 2]),  # 3
    "F": (3, 3, [-4, 2, 0, 8, 3, 14, 6, 1, 8]),  # 4: columns v1 v2 v3
    "G": (2, 3, [1, 2, 3, 4, 5, 6]),  # not square
    "H": (3, 3, [1, 2, 3, 2, 4, 6, 1, 0, 1]),  # not triangular
}


def matdet(s):
    # Operations: 0 Determinant, 1 Cofactor expansion, 2 Det (triangular),
    #             3 Adjoint adj(A), 4 Invertible?, 5 Indep.? (det)
    # (Mat2 has the same, then 6 Col(A) & Nul(A))
    s("snap main")
    enter_matrices(s, matrices=WORKSHEET_DET)
    s.press("down")
    s.press("enter")
    s("snap operations")
    s.press("clear")
    s.press("up")

    def expand(name, slot, column, index):
        # Operations -> Cofactor expansion -> pick -> choose the line -> run
        s.press("down")
        s.press("enter")
        s.press("down", 1)
        s.press("enter")
        pick(s, slot)
        s(f"snap {name}_line")
        if column:
            s.press("right")  # Row -> Column
        s.press("down")
        s.press("right", index)
        s(f"snap {name}_line_set")
        s.press("enter")
        s(f"snap {name}_result")
        steps(s, name, pages=6)
        s.press("clear")  # result -> Operations
        s.press("clear")
        s.press("up")

    expand("ws1a_row3", "A", False, 2)
    expand("ws1b_col1", "B", True, 0)
    expand("ws1b_col4", "B", True, 3)
    expand("ws3a_row1", "E", False, 0)
    run(s, "expand_nonsquare", [1], "G", error=True)
    run(s, "ws2a_triangular", [2], "C")
    run(s, "ws2b_triangular", [2], "D")
    run(s, "triangular_not", [2], "H", error=True)
    run(s, "ws3b_det", [0], "E", pages=6)
    run(s, "ws4_indep", [5], "F", pages=8)
    run(s, "indep_independent", [5], "A", pages=8)
    run(s, "indep_nonsquare", [5], "G", error=True)
    run(s, "invertible", [4], "F")
    run(s, "adjugate", [3], "E", pages=13)
    run(s, "adjugate_4x4", [3], "B", pages=4)
    run(s, "adjugate_nonsquare", [3], "G", error=True)


def mat2(s):
    # MatrixDet's operations, then MatrixBasis's, in one Operations menu
    matdet(s)
    s.press("down")
    s.press("enter")
    s.press("down", 6)
    s("snap operations_bottom")
    s.press("clear")
    s.press("up")
    matbasis(s, op_index=6, enter=False)


# the Math 210 Cramer's rule and areas worksheet
WORKSHEET_CRAM = {
    "A": (3, 3, [1, -1, 0, 3, -1, -3, 1, -3, -2]),  # 1(a) coefficients
    "B": (3, 1, [0, 2, -3]),  # 1(a) right hand side
    "C": (3, 3, [2, 0, 1, 0, 0, 2, 1, 1, 0]),  # 2(a)
    "D": (2, 4, [0, 4, 2, 6, 0, 1, 6, 7]),  # 3(a) parallelogram vertices
    "E": (2, 3, [4, 2, 3, 1, 6, 8]),  # 3(b) triangle vertices
    "F": (2, 2, [2, 4, -5, 1]),  # 4(a) spanning vectors
    "G": (2, 2, [3, 1, 1, 2]),  # 4(b) T(x) = Ax
    "H": (2, 4, [0, 4, 2, 7, 0, 1, 6, 7]),  # not a parallelogram
}


def matcram(s):
    # Operations: 0 Determinant, 1 Cofactor expansion, 2 Det (triangular),
    #             3 Adjoint adj(A), 4 Cofactor/Cramer, 5 Area, 6 Invertible?,
    #             7 Indep.? (det)
    # Cofactor/Cramer: 0 Cramer. Area: 0 parallelogram, 1 triangle, 2 T(P)
    s("snap main")
    enter_matrices(s, matrices=WORKSHEET_CRAM)
    s.press("down")
    s.press("enter")
    s("snap operations")
    s.press("down", 5)
    s.press("enter")
    s("snap menu_area")
    s.press("clear")
    s.press("clear")
    s.press("up")
    run(s, "ws1_cramer", [4, 0], "AB", has_steps=False, extra=cramer_steps)
    run(s, "ws1_inverse", [3], "A", pages=15)
    run(s, "ws2_adjugate", [3], "C", pages=15)
    run(s, "ws3a_parallelogram", [5, 0], "D", pages=4)
    run(s, "ws3b_triangle", [5, 1], "E", pages=3)
    run(s, "ws4a_parallelogram", [5, 0], "F", pages=2)
    run(s, "ws4b_transform", [5, 2], "GF", pages=7)
    run(s, "area_not_parallelogram", [5, 0], "H", error=True)
    run(s, "area_triangle_dims", [5, 1], "D", error=True)
    run(s, "area_transform_dims", [5, 2], "CF", error=True)


PROGRAMS = {
    "mat2": mat2,
    "matcram": matcram,
    "matdet": matdet,
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

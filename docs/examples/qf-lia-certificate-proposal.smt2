; Original example for the proposed integer-only certificate format.
; UNSAT over Int; the real relaxation has x = 1/2, y = 1.
(set-logic QF_LIA)
(declare-fun x () Int)
(declare-fun y () Int)
(assert (<= (+ (* 3 x) (* 2 y)) 4))
(assert (<= 1 y))
(assert (<= 1 (* 2 x)))
(check-sat)

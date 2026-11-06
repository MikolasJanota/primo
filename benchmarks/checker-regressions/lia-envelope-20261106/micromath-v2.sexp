(primo-conflicts
  (version 2)
  (logic QF_LIA)
  (signature
    (declare-fun x () Int)
    (declare-fun y () Int))
  (cnf
    (clause (<= (+ (* 3 x) (* 2 y)) 4))
    (clause (<= 1 y))
    (clause (<= 1 (* 2 x))))
  (conflicts
    (conflict-lia
      (premises
        (p0 (<= 1 (* 2 x)))
        (p1 (<= 1 y))
        (p2 (<= (+ (* 3 x) (* 2 y)) 4)))
      (proof
        (cut s0 2 (p0 1))
        (cut s1 1 (s0 3) (p2 1) (p1 2))
        (contradiction s1)))))

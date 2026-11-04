(primo-conflicts
  (version 2)
  (logic QF_LRA)
  (signature
    (declare-fun x () Real)
    (declare-fun y () Real))
  (cnf
    (clause (<= (+ (* 2 x) y) 1))
    (clause (<= 1 x))
    (clause (<= 0 y)))
  (conflicts
    (conflict-lra ((<= (+ (* 2 x) y) 1) 1) ((<= 1 x) 2) ((<= 0 y) 1))))

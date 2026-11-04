(primo-conflicts
  (version 2)
  (logic QF_LRA)
  (signature
    (declare-fun x () Real)
    (declare-fun y () Real))
  (cnf
    (clause (<= (+ x y) 2))
    (clause (<= 2 x))
    (clause (< 0 y)))
  (conflicts
    (conflict-lra ((<= (+ x y) 2) 1) ((<= 2 x) 1) ((< 0 y) 1))))

(primo-conflicts
  (version 2)
  (logic QF_LRA)
  (signature
    (declare-fun x () Real)
    (declare-fun y () Real))
  (cnf
    (clause (<= x y))
    (clause (<= y x))
    (clause (< y x)))
  (conflicts
    (conflict-lra ((<= x y) 1) ((< y x) 1))))

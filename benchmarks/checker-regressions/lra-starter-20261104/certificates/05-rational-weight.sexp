(primo-conflicts
  (version 2)
  (logic QF_LRA)
  (signature
    (declare-fun x () Real))
  (cnf
    (clause (<= (* 2 x) 0))
    (clause (< 0 x)))
  (conflicts
    (conflict-lra ((<= (* 2 x) 0) (/ 1 2)) ((< 0 x) 1))))

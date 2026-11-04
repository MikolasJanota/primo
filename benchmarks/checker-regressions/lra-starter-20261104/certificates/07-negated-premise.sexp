(primo-conflicts
  (version 2)
  (logic QF_LRA)
  (signature
    (declare-fun x () Real)
    (declare-fun y () Real))
  (cnf
    (clause (not (< x 0)))
    (clause (<= 0 y))
    (clause (<= (+ x y) (- 1))))
  (conflicts
    (conflict-lra ((<= (+ x y) (- 1)) 1) ((not (< x 0)) 1) ((<= 0 y) 1))))

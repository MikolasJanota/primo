; Authored adapter example, not a native solver output.
(primo-conflicts
  (version 2)
  (logic QF_LRA)
  (signature (declare-fun x () Real) (declare-fun y () Real))
  (cnf
    (clause (<= (* 31 x) 0))
    (clause (<= (* 31 y) 0))
    (clause (<= (+ (* (- 7) x) (* (- 20) y)) 0))
    (clause (<= (+ (* (- 34) x) (* (- 13) y)) (- 1))))
  (conflicts
    (conflict-lra
      ((<= (* 31 x) 0) 11)
      ((<= (* 31 y) 0) 7)
      ((<= (+ (* (- 7) x) (* (- 20) y)) 0) 5)
      ((<= (+ (* (- 34) x) (* (- 13) y)) (- 1)) 9))))

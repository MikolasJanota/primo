(primo-conflicts
  (version 2)
  (logic QF_LRA)
  (signature
    (declare-fun x () Real))
  (cnf
    (clause (<= (* (/ 2 3) x) (- (/ 1 5))))
    (clause (<= 0 x)))
  (conflicts
    (conflict-lra ((<= (* (/ 2 3) x) (- (/ 1 5))) 5) ((<= 0 x) (/ 10 3)))))

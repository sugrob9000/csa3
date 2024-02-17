(set str (alloc-static 10))
(set ptr str)
(set len 0)
(while (set in (read-mem -1))
       (progn
         (set ptr (+ ptr 1))
         (set len (+ len 1))
         (write-mem ptr in)))
(write-mem str len)
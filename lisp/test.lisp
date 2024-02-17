(set lives-long-1 1024000)
(set lives-long-2 1024001)
(set lives-long-3 1024002)

(set x 0)
(while (< x 20)
       (progn
         (set x (+ x 1))))
(while 1 1)
(set x 10241024)

(read-mem lives-long-1)
(read-mem lives-long-2)
(read-mem lives-long-3)

(set x (+ lives-long-1 2048000))
(set y (+ lives-long-2 2048001))
(set z (+ lives-long-3 2048002))
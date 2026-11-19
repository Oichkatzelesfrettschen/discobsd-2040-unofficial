;; Conformance cases of uLisp against ANSI INCITS 226-1994 (Common Lisp), written
;; for this port: each case cites the section of the Common Lisp HyperSpec that
;; states the behavior it checks, and the expected value is the one that
;; standard or a conforming implementation (SBCL, ECL) gives. A passing case
;; prints its tag; a failing one prints (fail tag got want). Differences that
;; are design choices of uLisp are recorded in
;; docs/research/ulisp-common-lisp-conformance.md and are absent here.
(defun ck (tag got want) (if (equal got want) tag (list 'fail tag got want)))
;; CLHS 2.2 READ (f_rd_rd.htm)
(list
 (ck "2.2 READ 1" (list (read-from-string "(1 2)") (read-from-string "  42  ") (read-from-string "abc def")) '((1 2) 42 abc)))
;; CLHS 2.3.1 READ (f_rd_rd.htm)
(list
 (ck "2.3.1 READ 1" '1 1)
 (ck "2.3.1 READ 2" '+5 5)
 (ck "2.3.1 READ 3" '-5 -5)
 (ck "2.3.1 READ 4" '007 7)
 (ck "2.3.1 READ 5" (floatp '.5) t)
 (ck "2.3.1 READ 6" (= '.5 0.5) t)
 (ck "2.3.1 READ 7" (= '-.5e-3 -0.0005) t)
 (ck "2.3.1 READ 8" (= '1.5e3 1500.0) t)
 (ck "2.3.1 READ 9" (= '1E5 100000.0) t)
 (ck "2.3.1 READ 10" (floatp '1e5) t)
 (ck "2.3.1 READ 11" (floatp '1.5) t)
 (ck "2.3.1 READ 12" (integerp '12) t)
 (ck "2.3.1 READ 13" (list (symbolp '1+) (symbolp '-) (symbolp '+) (symbolp 'e5) (symbolp '1-) (symbolp '+a)) '(t t t t t t)))
;; CLHS 2.3.2 READ (f_rd_rd.htm)
(list
 (ck "2.3.2 READ 1" (list '#xFF '#x1f '#b101 '#o17 '#X1F) '(255 31 5 15 31)))
;; CLHS 2.3.3 READ (f_rd_rd.htm)
(list
 (ck "2.3.3 READ 1" '(a . b) '(a . b))
 (ck "2.3.3 READ 2" '(a . (b c)) '(a b c))
 (ck "2.3.3 READ 3" '(1 . (2 . (3))) '(1 2 3))
 (ck "2.3.3 READ 4" (length '(1 .5)) 2))
;; CLHS 2.3.4 READ (f_rd_rd.htm)
(list
 (ck "2.3.4 READ 1" (eq 'abc 'ABC) t)
 (ck "2.3.4 READ 2" (eq 'abc 'abd) nil)
 (ck "2.3.4 READ 3" (keywordp ':k) t)
 (ck "2.3.4 READ 4" (symbolp 'nil) t)
 (ck "2.3.4 READ 5" (eq 'nil nil) t))
;; CLHS 2.4.1 READ (f_rd_rd.htm)
(list
 (ck "2.4.1 READ 1" (list '() '( ) '(  1   2  )) '(nil nil (1 2))))
;; CLHS 2.4.3 READ (f_rd_rd.htm)
(list
 (ck "2.4.3 READ 1" (car ''a) 'quote)
 (ck "2.4.3 READ 2" ''a '(quote a))
 (ck "2.4.3 READ 3" (list 'a'b) '(a b))
 (ck "2.4.3 READ 4" (cadr ''(a b)) '(a b)))
;; CLHS 2.4.4 READ (f_rd_rd.htm)
(list
 (ck "2.4.4 READ 1" (list 1 ; two (
   2 ; ) three ;; four
   3) '(1 2 3))
 (ck "2.4.4 READ 2" (list 'a;b
   'c) '(a c))
 (ck "2.4.4 READ 3" (length "a;b") 3)
 (ck "2.4.4 READ 4" (char-code #\;) 59))
;; CLHS 2.4.5 READ (f_rd_rd.htm)
(list
 (ck "2.4.5 READ 1" (list (length "a(b") (length "a)b") (length "a|b") (length "")) '(3 3 3 0))
 (ck "2.4.5 READ 2" (length "a\"b") 3)
 (ck "2.4.5 READ 3" (char "a\\b" 1) #\\)
 (ck "2.4.5 READ 4" (length "a#|b|#c") 7))
;; CLHS 2.4.8.1 READ (f_rd_rd.htm)
(list
 (ck "2.4.8.1 READ 1" (list (char-code #\a) (char-code #\A) (char-code #\Space) (char-code #\space) (char-code #\Newline) (char-code #\newline)) '(97 65 32 32 10 10))
 (ck "2.4.8.1 READ 2" (list (char-code #\() (char-code #\)) (char-code #\;) (char-code #\") (char-code #\#) (char-code #\')) '(40 41 59 34 35 39))
 (ck "2.4.8.1 READ 3" (list (char-code #\Tab) (char-code #\Return) (char-code #\Backspace) (char-code #\Page)) '(9 13 8 12)))
;; CLHS 2.4.8.2 READ (f_rd_rd.htm)
(list
 (ck "2.4.8.2 READ 1" (funcall #'car '(1 2)) 1)
 (ck "2.4.8.2 READ 2" (mapcar #'1+ '(1 2)) '(2 3)))
;; CLHS 2.4.8.19 READ (f_rd_rd.htm)
(list
 (ck "2.4.8.19 READ 1" (list 1 #| a |# 2 #|| b ||# 3) '(1 2 3))
 (ck "2.4.8.19 READ 2" (list 1 #| a #| nested |# still comment ; ( |# 2) '(1 2))
 (ck "2.4.8.19 READ 3" (list 1 #| ; |# 2 #| ( |# 3) '(1 2 3)))
;; CLHS 3.1 LET (s_let_l.htm)
(list
 (ck "3.1 LET 1" (let ((a 1) (b 2)) (+ a b)) 3)
 (ck "3.1 LET 2" (let ((a 1)) (let ((a 2) (b a)) b)) 1)
 (ck "3.1 LET 3" (let (a) a) nil)
 (ck "3.1 LET 4" (ignore-errors (let ((a 1) (b a)) b)) 'nothing)
 (ck "3.1 LET 5" (let () 5) 5)
 (ck "3.1 LET 6" (let ((a 1))) nil))
;; CLHS 3.4.1 &OPTIONAL (03_da.htm)
(list
 (ck "3.4.1 &OPTIONAL 1" (progn (defun o1 (&optional a b) (list a b)) (o1 1)) '(1 nil)))
;; CLHS 3.4.1 &REST (03_da.htm)
(list
 (ck "3.4.1 &REST 1" (progn (defun r1 (&rest a) a) (r1)) nil))
;; CLHS 3.8 DEFVAR (m_defpar.htm)
(list
 (ck "3.8 DEFVAR 1" (defvar v1 5) 'v1)
 (ck "3.8 DEFVAR 2" (defvar v3) 'v3))
;; CLHS 3.8 QUOTE (s_quote.htm)
(list
 (ck "3.8 QUOTE 1" '(a . b) '(a . b)))
;; CLHS 5.3 AND (a_and.htm)
(list
 (ck "5.3 AND 1" (and 1 2) 2)
 (ck "5.3 AND 2" (and) t)
 (ck "5.3 AND 3" (and 1 nil 3) nil))
;; CLHS 5.3 APPLY (f_apply.htm)
(list
 (ck "5.3 APPLY 1" (apply #'+ 1 2 '(3 4)) 10)
 (ck "5.3 APPLY 2" (apply #'+ '(1 2)) 3)
 (ck "5.3 APPLY 3" (apply '+ '(1 2)) 3))
;; CLHS 5.3 CASE (m_case_.htm)
(list
 (ck "5.3 CASE 1" (case 2 (1 'a) (2 'b)) 'b)
 (ck "5.3 CASE 2" (case 2 ((1 2) 'a) (t 'c)) 'a)
 (ck "5.3 CASE 3" (case 5 (1 'a)) nil)
 (ck "5.3 CASE 4" (case 'x (x 1) (t 2)) 1)
 (ck "5.3 CASE 5" (case 2 ((1) 'a) (t 'c)) 'c))
;; CLHS 5.3 COND (m_cond.htm)
(list
 (ck "5.3 COND 1" (cond (nil 1) (t 2)) 2)
 (ck "5.3 COND 2" (cond (5)) 5)
 (ck "5.3 COND 3" (cond ((= 1 2) 1)) nil)
 (ck "5.3 COND 4" (cond (nil 1) (2 3 4)) 4))
;; CLHS 5.3 DEFUN (m_defun.htm)
(list
 (ck "5.3 DEFUN 1" (progn (defun f1 (x) (* 2 x)) (f1 4)) 8)
 (ck "5.3 DEFUN 2" (defun f2 (x) x) 'f2)
 (ck "5.3 DEFUN 3" (progn (defun f3 (a &optional b) (list a b)) (list (f3 1) (f3 1 2))) '((1 nil) (1 2)))
 (ck "5.3 DEFUN 4" (progn (defun f4 (a &rest r) (list a r)) (f4 1 2 3)) '(1 (2 3)))
 (ck "5.3 DEFUN 5" (progn (defun f5 (a) "doc" a) (f5 1)) 1)
 (ck "5.3 DEFUN 6" (ignore-errors (progn (defun f6 (a) (+ a 1)) (f6 1 2))) 'nothing)
 (ck "5.3 DEFUN 7" (ignore-errors (progn (defun f7 (a) (+ a 1)) (f7))) 'nothing)
 (ck "5.3 DEFUN 8" (progn (defun f8 (&optional (a 5)) a) (f8)) 5)
 (ck "5.3 DEFUN 9" (progn (defun f9 (&key a) a) (f9 :a 3)) 3))
;; CLHS 5.3 EQ (f_eq.htm)
(list
 (ck "5.3 EQ 1" (eq 'a 'a) t)
 (ck "5.3 EQ 2" (eq nil '()) t))
;; CLHS 5.3 EQUAL (f_equal.htm)
(list
 (ck "5.3 EQUAL 1" (equal '(1 2) '(1 2)) t)
 (ck "5.3 EQUAL 2" (equal "ab" "ab") t)
 (ck "5.3 EQUAL 3" (equal 1 1.0) nil)
 (ck "5.3 EQUAL 4" (equal 1.0 1.0) t)
 (ck "5.3 EQUAL 5" (equal "a" "A") nil)
 (ck "5.3 EQUAL 6" (equal 'a 'a) t))
;; CLHS 5.3 FUNCALL (f_funcal.htm)
(list
 (ck "5.3 FUNCALL 1" (funcall #'+ 1 2) 3)
 (ck "5.3 FUNCALL 2" (funcall (lambda (x) (* x x)) 5) 25)
 (ck "5.3 FUNCALL 3" (funcall 'car '(1)) 1)
 (ck "5.3 FUNCALL 4" (funcall #'(lambda (x) x) 5) 5))
;; CLHS 5.3 IF (s_if.htm)
(list
 (ck "5.3 IF 1" (if t 1 2) 1)
 (ck "5.3 IF 2" (if nil 1) nil)
 (ck "5.3 IF 3" (ignore-errors (if nil 1 2 3)) 'nothing)
 (ck "5.3 IF 4" (if 0 'yes 'no) 'yes)
 (ck "5.3 IF 5" (if '() 'yes 'no) 'no))
;; CLHS 5.3 LAMBDA (a_lambda.htm)
(list
 (ck "5.3 LAMBDA 1" ((lambda (x y) (+ x y)) 1 2) 3)
 (ck "5.3 LAMBDA 2" ((lambda (&optional x) x)) nil)
 (ck "5.3 LAMBDA 3" ((lambda (&rest r) r) 1 2) '(1 2))
 (ck "5.3 LAMBDA 4" (funcall (lambda () 5)) 5))
;; CLHS 5.3 LET* (s_let_l.htm)
(list
 (ck "5.3 LET* 1" (let* ((a 1) (b (+ a 1))) b) 2)
 (ck "5.3 LET* 2" (let* ((a 1) (a (+ a 1))) a) 2)
 (ck "5.3 LET* 3" (let* () 5) 5))
;; CLHS 5.3 NIL (a_nil.htm)
(list
 (ck "5.3 NIL 1" (list nil '() 'nil) '(nil nil nil))
 (ck "5.3 NIL 2" (symbolp nil) t)
 (ck "5.3 NIL 3" (listp nil) t)
 (ck "5.3 NIL 4" (atom nil) t))
;; CLHS 5.3 NOT (a_not.htm)
(list
 (ck "5.3 NOT 1" (not nil) t)
 (ck "5.3 NOT 2" (not 5) nil))
;; CLHS 5.3 OR (a_or.htm)
(list
 (ck "5.3 OR 1" (or nil 5) 5)
 (ck "5.3 OR 2" (or) nil)
 (ck "5.3 OR 3" (or nil nil) nil))
;; CLHS 5.3 PROGN (s_progn.htm)
(list
 (ck "5.3 PROGN 1" (progn 1 2 3) 3)
 (ck "5.3 PROGN 2" (progn) nil))
;; CLHS 5.3 PUSH (m_push.htm)
(list
 (ck "5.3 PUSH 1" (let ((l nil)) (push 1 l) (push 2 l) l) '(2 1))
 (ck "5.3 PUSH 2" (let ((l (list 1 2 3))) (list (pop l) l)) '(1 (2 3))))
;; CLHS 5.3 RETURN (m_return.htm)
(list
 (ck "5.3 RETURN 1" (dotimes (i 5) (when (= i 2) (return i))) 2)
 (ck "5.3 RETURN 2" (loop (return 7)) 7))
;; CLHS 5.3 SETF (a_setf.htm)
(list
 (ck "5.3 SETF 1" (let ((a (list 1 2 3))) (setf (car a) 9) a) '(9 2 3))
 (ck "5.3 SETF 2" (let ((a (list 1 2 3))) (setf (cdr a) 9) a) '(1 . 9))
 (ck "5.3 SETF 3" (let ((a (list 1 2 3))) (setf (nth 1 a) 9) a) '(1 9 3))
 (ck "5.3 SETF 4" (let ((a 1) (b 2)) (setf a 5 b 6) (list a b)) '(5 6))
 (ck "5.3 SETF 5" (let ((a (list 1 2 3))) (setf (rest a) nil) a) '(1)))
;; CLHS 5.3 SETQ (s_setq.htm)
(list
 (ck "5.3 SETQ 1" (let ((a 1)) (setq a 2) a) 2)
 (ck "5.3 SETQ 2" (let ((a 1) (b 1)) (setq a 5 b a) b) 5))
;; CLHS 5.3 T (a_t.htm)
(list
 (ck "5.3 T 1" (eq t 't) t)
 (ck "5.3 T 2" (symbolp t) t))
;; CLHS 5.3 UNLESS (m_when_.htm)
(list
 (ck "5.3 UNLESS 1" (unless nil 1 2) 2)
 (ck "5.3 UNLESS 2" (unless t 1) nil))
;; CLHS 5.3 UNWIND-PROTECT (s_unwind.htm)
(list
 (ck "5.3 UNWIND-PROTECT 1" (let ((a 0)) (unwind-protect (setq a 1) (setq a 2)) a) 2)
 (ck "5.3 UNWIND-PROTECT 2" (unwind-protect 5 6) 5))
;; CLHS 5.3 WHEN (m_when_.htm)
(list
 (ck "5.3 WHEN 1" (when t 1 2) 2)
 (ck "5.3 WHEN 2" (when nil 1) nil)
 (ck "5.3 WHEN 3" (when 1) nil))
;; CLHS 6.2 DO (m_do_do.htm)
(list
 (ck "6.2 DO 1" (do ((i 0 (1+ i)) (s 0 (+ s i))) ((= i 4) s)) 6)
 (ck "6.2 DO 2" (do ((i 0 (1+ i))) ((= i 3))) nil)
 (ck "6.2 DO 3" (do ((i 0 (1+ i)) (j 10 (1- j))) ((>= i j) (list i j))) '(5 5))
 (ck "6.2 DO 4" (ignore-errors (do ((i 0 (1+ i)) (j i (+ j 1))) ((= i 3) j))) 'nothing))
;; CLHS 6.2 DO* (m_do_do.htm)
(list
 (ck "6.2 DO* 1" (do* ((i 0 (1+ i)) (j i (+ j 1))) ((= i 3) j)) 3))
;; CLHS 6.2 DOLIST (m_dolist.htm)
(list
 (ck "6.2 DOLIST 1" (let ((r 0)) (dolist (x '(1 2 3)) (incf r x)) r) 6)
 (ck "6.2 DOLIST 2" (dolist (x '(1 2 3))) nil)
 (ck "6.2 DOLIST 3" (let ((r 0)) (dolist (x '(1 2 3) r) (incf r x))) 6)
 (ck "6.2 DOLIST 4" (let ((r nil)) (dolist (x '(1 2 3) (reverse r)) (push x r))) '(1 2 3))
 (ck "6.2 DOLIST 5" (dolist (x '(1 2 3) x)) nil)
 (ck "6.2 DOLIST 6" (dolist (x nil 5)) 5))
;; CLHS 6.2 DOTIMES (m_dotime.htm)
(list
 (ck "6.2 DOTIMES 1" (let ((r 0)) (dotimes (i 4) (incf r i)) r) 6)
 (ck "6.2 DOTIMES 2" (dotimes (i 3)) nil)
 (ck "6.2 DOTIMES 3" (let ((r 0)) (dotimes (i 4 (list r i)) (incf r i))) '(6 4))
 (ck "6.2 DOTIMES 4" (dotimes (i 0 'done)) 'done)
 (ck "6.2 DOTIMES 5" (dotimes (i -1 'done)) 'done))
;; CLHS 6.2 LOOP (m_loop.htm)
(list
 (ck "6.2 LOOP 1" (let ((i 0)) (loop (incf i) (when (> i 3) (return i)))) 4))
;; CLHS 9.2 ERROR (a_error.htm)
(list
 (ck "9.2 ERROR 1" (ignore-errors (error "boom")) 'nothing)
 (ck "9.2 ERROR 2" (ignore-errors (error "boom" 5)) 'nothing))
;; CLHS 9.2 IGNORE-ERRORS (m_ignore.htm)
(list
 (ck "9.2 IGNORE-ERRORS 1" (ignore-errors 7) 7))
;; CLHS 10.2 BOUNDP (f_boundp.htm)
(list
 (ck "10.2 BOUNDP 1" (boundp 'nosuchvar) nil))
;; CLHS 10.2 KEYWORDP (f_kwdp.htm)
(list
 (ck "10.2 KEYWORDP 1" (keywordp 'a) nil))
;; CLHS 10.2 MAKUNBOUND (f_makunb.htm)
(list
 (ck "10.2 MAKUNBOUND 1" (progn (defvar mb 1) (makunbound 'mb) (boundp 'mb)) nil))
;; CLHS 10.2 SYMBOLP (f_symbol.htm)
(list
 (ck "10.2 SYMBOLP 1" (symbolp nil) t)
 (ck "10.2 SYMBOLP 2" (symbolp 'a) t)
 (ck "10.2 SYMBOLP 3" (symbolp :k) t)
 (ck "10.2 SYMBOLP 4" (symbolp "a") nil))
;; CLHS 12.2 * (a_st.htm)
(list
 (ck "12.2 * 1" (* 2 3 4) 24)
 (ck "12.2 * 2" (*) 1)
 (ck "12.2 * 3" (* 2 1.5) '3.0))
;; CLHS 12.2 + (a_pl.htm)
(list
 (ck "12.2 + 1" (+ 1 2) 3)
 (ck "12.2 + 2" (+) 0)
 (ck "12.2 + 3" (+ 1.5 2) '3.5)
 (ck "12.2 + 4" (+ 1073741823 1) 1073741824)
 (ck "12.2 + 5" (ignore-errors (+ 1 'a)) 'nothing))
;; CLHS 12.2 - (a__.htm)
(list
 (ck "12.2 - 1" (- 5) -5)
 (ck "12.2 - 2" (- 5 3 1) 1)
 (ck "12.2 - 3" (ignore-errors (-)) 'nothing))
;; CLHS 12.2 / (a_sl.htm)
(list
 (ck "12.2 / 1" (/ 12 3) 4)
 (ck "12.2 / 2" (/ 7 2.0) '3.5)
 (ck "12.2 / 3" (/ 4 2 2) 1)
 (ck "12.2 / 4" (ignore-errors (/ 1 0)) 'nothing)
 (ck "12.2 / 5" (ignore-errors (/ 1.0 0)) 'nothing))
;; CLHS 12.2 /= (f_eq_sle.htm)
(list
 (ck "12.2 /= 1" (/= 1 2) t)
 (ck "12.2 /= 2" (/= 1 1) nil)
 (ck "12.2 /= 3" (/= 1 2 1) nil)
 (ck "12.2 /= 4" (/= 1 1.0) nil))
;; CLHS 12.2 1+ (f_1pl_1_.htm)
(list
 (ck "12.2 1+ 1" (1+ 5) 6)
 (ck "12.2 1+ 2" (1+ 1.5) '2.5))
;; CLHS 12.2 1- (f_1pl_1_.htm)
(list
 (ck "12.2 1- 1" (1- 5) 4))
;; CLHS 12.2 < (f_eq_sle.htm)
(list
 (ck "12.2 < 1" (< 1 2 3) t)
 (ck "12.2 < 2" (< 1 3 2) nil)
 (ck "12.2 < 3" (< 1 1.5) t))
;; CLHS 12.2 <= (f_eq_sle.htm)
(list
 (ck "12.2 <= 1" (<= 1 1 2) t))
;; CLHS 12.2 = (f_eq_sle.htm)
(list
 (ck "12.2 = 1" (= 1 1.0) t)
 (ck "12.2 = 2" (= 1 1 1) t)
 (ck "12.2 = 3" (= 1 2) nil))
;; CLHS 12.2 > (f_eq_sle.htm)
(list
 (ck "12.2 > 1" (> 3 2 1) t))
;; CLHS 12.2 >= (f_eq_sle.htm)
(list
 (ck "12.2 >= 1" (>= 3 3 1) t))
;; CLHS 12.2 ABS (f_abs.htm)
(list
 (ck "12.2 ABS 1" (abs -5) 5)
 (ck "12.2 ABS 2" (abs -2.5) '2.5)
 (ck "12.2 ABS 3" (abs 5) 5))
;; CLHS 12.2 ASH (f_ash.htm)
(list
 (ck "12.2 ASH 1" (ash 1 3) 8)
 (ck "12.2 ASH 2" (ash 16 -2) 4)
 (ck "12.2 ASH 3" (ash -8 -1) -4)
 (ck "12.2 ASH 4" (ash 1 30) 1073741824)
 (ck "12.2 ASH 5" (ash 1 3) 8)
 (ck "12.2 ASH 6" (ash 16 -2) 4)
 (ck "12.2 ASH 7" (ash -8 -1) -4)
 (ck "12.2 ASH 8" (ash -5 -40) -1)
 (ck "12.2 ASH 9" (ash 5 -40) 0)
 (ck "12.2 ASH 10" (ash 1 0) 1))
;; CLHS 12.2 CEILING (f_floorc.htm)
(list
 (ck "12.2 CEILING 1" (ceiling 7 2) 4)
 (ck "12.2 CEILING 2" (ceiling -7 2) -3)
 (ck "12.2 CEILING 3" (ceiling 2.5) 3)
 (ck "12.2 CEILING 4" (ceiling 5) 5)
 (ck "12.2 CEILING 5" (ceiling 5 2) 3)
 (ck "12.2 CEILING 6" (ceiling -5 2) -2)
 (ck "12.2 CEILING 7" (ceiling 5 -2) -2)
 (ck "12.2 CEILING 8" (ceiling -5 -2) 3)
 (ck "12.2 CEILING 9" (ceiling 2.1) 3)
 (ck "12.2 CEILING 10" (ignore-errors (ceiling 5 0)) 'nothing))
;; CLHS 12.2 COS (f_sin_c.htm)
(list
 (ck "12.2 COS 1" (cos 0) '1.0))
;; CLHS 12.2 DECF (m_incf_.htm)
(list
 (ck "12.2 DECF 1" (let ((a 1)) (decf a 3)) -2))
;; CLHS 12.2 EVENP (f_evenpc.htm)
(list
 (ck "12.2 EVENP 1" (evenp 0) t)
 (ck "12.2 EVENP 2" (evenp -2) t))
;; CLHS 12.2 EXP (f_exp_e.htm)
(list
 (ck "12.2 EXP 1" (exp 0) '1.0))
;; CLHS 12.2 EXPT (f_exp_e.htm)
(list
 (ck "12.2 EXPT 1" (expt 2 3) 8)
 (ck "12.2 EXPT 2" (expt 2.0 3) '8.0)
 (ck "12.2 EXPT 3" (expt 2 30) 1073741824)
 (ck "12.2 EXPT 4" (expt -8 3) -512)
 (ck "12.2 EXPT 5" (expt 10 2) 100))
;; CLHS 12.2 FLOAT (a_float.htm)
(list
 (ck "12.2 FLOAT 1" (float 1) '1.0)
 (ck "12.2 FLOAT 2" (float 1.5) '1.5))
;; CLHS 12.2 FLOATP (f_floatp.htm)
(list
 (ck "12.2 FLOATP 1" (floatp 1.0) t)
 (ck "12.2 FLOATP 2" (floatp 1) nil))
;; CLHS 12.2 FLOOR (f_floorc.htm)
(list
 (ck "12.2 FLOOR 1" (floor 7 2) 3)
 (ck "12.2 FLOOR 2" (floor -7 2) -4)
 (ck "12.2 FLOOR 3" (floor 7 -2) -4)
 (ck "12.2 FLOOR 4" (floor 2.7) 2)
 (ck "12.2 FLOOR 5" (floor -2.7) -3)
 (ck "12.2 FLOOR 6" (floor 5 2) 2)
 (ck "12.2 FLOOR 7" (floor -5 2) -3)
 (ck "12.2 FLOOR 8" (floor 5 -2) -3)
 (ck "12.2 FLOOR 9" (floor -5 -2) 2)
 (ck "12.2 FLOOR 10" (floor 2.7) 2)
 (ck "12.2 FLOOR 11" (floor -2.7) -3)
 (ck "12.2 FLOOR 12" (ignore-errors (floor 5 0)) 'nothing))
;; CLHS 12.2 INCF (m_incf_.htm)
(list
 (ck "12.2 INCF 1" (let ((a 1)) (incf a)) 2)
 (ck "12.2 INCF 2" (let ((a 1)) (incf a 5) a) 6)
 (ck "12.2 INCF 3" (let ((a 1.5)) (incf a)) '2.5))
;; CLHS 12.2 INTEGERP (f_inte_1.htm)
(list
 (ck "12.2 INTEGERP 1" (integerp 1) t)
 (ck "12.2 INTEGERP 2" (integerp 1.0) nil))
;; CLHS 12.2 LOG (f_log.htm)
(list
 (ck "12.2 LOG 1" (log 100 10) '2.0)
 (ck "12.2 LOG 2" (log 8 2) '3.0)
 (ck "12.2 LOG 3" (log 2.718282) '1.0))
;; CLHS 12.2 LOGAND (f_logand.htm)
(list
 (ck "12.2 LOGAND 1" (logand 12 10) 8)
 (ck "12.2 LOGAND 2" (logand) -1)
 (ck "12.2 LOGAND 3" (logand 12 10) 8))
;; CLHS 12.2 LOGBITP (f_logbtp.htm)
(list
 (ck "12.2 LOGBITP 1" (logbitp 1 2) t)
 (ck "12.2 LOGBITP 2" (logbitp 0 2) nil))
;; CLHS 12.2 LOGIOR (f_logand.htm)
(list
 (ck "12.2 LOGIOR 1" (logior 12 10) 14)
 (ck "12.2 LOGIOR 2" (logior) 0)
 (ck "12.2 LOGIOR 3" (logior 12 10) 14))
;; CLHS 12.2 LOGNOT (f_logand.htm)
(list
 (ck "12.2 LOGNOT 1" (lognot 5) -6)
 (ck "12.2 LOGNOT 2" (lognot 0) -1)
 (ck "12.2 LOGNOT 3" (lognot 5) -6))
;; CLHS 12.2 LOGXOR (f_logand.htm)
(list
 (ck "12.2 LOGXOR 1" (logxor 12 10) 6)
 (ck "12.2 LOGXOR 2" (logxor 12 10) 6))
;; CLHS 12.2 MAX (f_max_m.htm)
(list
 (ck "12.2 MAX 1" (max 1 5 3) 5)
 (ck "12.2 MAX 2" (max 1 2.5) '2.5)
 (ck "12.2 MAX 3" (max 3) 3)
 (ck "12.2 MAX 4" (max 1 3.0) '3.0))
;; CLHS 12.2 MIN (f_max_m.htm)
(list
 (ck "12.2 MIN 1" (min 4 2 8) 2)
 (ck "12.2 MIN 2" (min 3 1.0) '1.0))
;; CLHS 12.2 MINUSP (f_minusp.htm)
(list
 (ck "12.2 MINUSP 1" (minusp -1) t)
 (ck "12.2 MINUSP 2" (minusp 0) nil))
;; CLHS 12.2 MOD (a_mod.htm)
(list
 (ck "12.2 MOD 1" (mod 13 4) 1)
 (ck "12.2 MOD 2" (mod -13 4) 3)
 (ck "12.2 MOD 3" (mod 13 -4) -3)
 (ck "12.2 MOD 4" (mod -13 -4) -1)
 (ck "12.2 MOD 5" (ignore-errors (mod 5 0)) 'nothing)
 (ck "12.2 MOD 6" (mod -1 5) 4)
 (ck "12.2 MOD 7" (mod 13 4) 1)
 (ck "12.2 MOD 8" (mod -13 4) 3)
 (ck "12.2 MOD 9" (mod 13 -4) -3)
 (ck "12.2 MOD 10" (mod -13 -4) -1)
 (ck "12.2 MOD 11" (mod 5 -1) 0)
 (ck "12.2 MOD 12" (mod 6 -3) 0)
 (ck "12.2 MOD 13" (mod 0 -3) 0)
 (ck "12.2 MOD 14" (mod 5 3) 2)
 (ck "12.2 MOD 15" (mod -5 3) 1)
 (ck "12.2 MOD 16" (< (abs (- (mod 13.4 1) 0.4)) 0.0001) t)
 (ck "12.2 MOD 17" (< (abs (- (mod -13.4 1) 0.6)) 0.0001) t)
 (ck "12.2 MOD 18" (mod 4.0 -2) 0.0)
 (ck "12.2 MOD 19" (mod 5.5 -2) -0.5)
 (ck "12.2 MOD 20" (ignore-errors (mod 5 0)) 'nothing))
;; CLHS 12.2 NUMBERP (f_nump.htm)
(list
 (ck "12.2 NUMBERP 1" (numberp 1.5) t)
 (ck "12.2 NUMBERP 2" (numberp 'a) nil))
;; CLHS 12.2 ODDP (f_evenpc.htm)
(list
 (ck "12.2 ODDP 1" (oddp 3) t)
 (ck "12.2 ODDP 2" (oddp -3) t))
;; CLHS 12.2 PLUSP (f_minusp.htm)
(list
 (ck "12.2 PLUSP 1" (plusp 1) t)
 (ck "12.2 PLUSP 2" (plusp 0) nil)
 (ck "12.2 PLUSP 3" (plusp -1.5) nil))
;; CLHS 12.2 REM (f_mod_r.htm)
(list
 (ck "12.2 REM 1" (rem -1 5) -1)
 (ck "12.2 REM 2" (rem 13 -4) 1)
 (ck "12.2 REM 3" (rem -13 4) -1)
 (ck "12.2 REM 4" (rem -13 -4) -1)
 (ck "12.2 REM 5" (ignore-errors (rem 5 0)) 'nothing)
 (ck "12.2 REM 6" (rem -1 5) -1)
 (ck "12.2 REM 7" (rem 13 4) 1)
 (ck "12.2 REM 8" (rem -13 4) -1)
 (ck "12.2 REM 9" (rem 13 -4) 1)
 (ck "12.2 REM 10" (rem -13 -4) -1)
 (ck "12.2 REM 11" (rem 5 -1) 0)
 (ck "12.2 REM 12" (< (abs (- (rem -13.4 1) -0.4)) 0.0001) t)
 (ck "12.2 REM 13" (rem 4.0 -2) 0.0)
 (ck "12.2 REM 14" (ignore-errors (rem 5 0)) 'nothing))
;; CLHS 12.2 ROUND (f_floorc.htm)
(list
 (ck "12.2 ROUND 1" (round 3.5) 4)
 (ck "12.2 ROUND 2" (round 7 2) 4)
 (ck "12.2 ROUND 3" (round 1.4) 1)
 (ck "12.2 ROUND 4" (round 2.5) 2)
 (ck "12.2 ROUND 5" (round 3.5) 4)
 (ck "12.2 ROUND 6" (round -2.5) -2)
 (ck "12.2 ROUND 7" (round -3.5) -4)
 (ck "12.2 ROUND 8" (round 0.5) 0)
 (ck "12.2 ROUND 9" (round 1.5) 2)
 (ck "12.2 ROUND 10" (round 7 2) 4)
 (ck "12.2 ROUND 11" (round 5 2) 2)
 (ck "12.2 ROUND 12" (round -5 2) -2)
 (ck "12.2 ROUND 13" (round 1.4) 1)
 (ck "12.2 ROUND 14" (ignore-errors (round 5 0)) 'nothing))
;; CLHS 12.2 SQRT (f_sqrt_.htm)
(list
 (ck "12.2 SQRT 1" (sqrt 16) '4.0))
;; CLHS 12.2 TRUNCATE (f_floorc.htm)
(list
 (ck "12.2 TRUNCATE 1" (truncate -7 2) -3)
 (ck "12.2 TRUNCATE 2" (truncate 7 2) 3)
 (ck "12.2 TRUNCATE 3" (truncate -2.7) -2)
 (ck "12.2 TRUNCATE 4" (truncate 5 2) 2)
 (ck "12.2 TRUNCATE 5" (truncate -5 2) -2)
 (ck "12.2 TRUNCATE 6" (truncate 5 -2) -2)
 (ck "12.2 TRUNCATE 7" (truncate -2.7) -2)
 (ck "12.2 TRUNCATE 8" (ignore-errors (truncate 5 0)) 'nothing))
;; CLHS 12.2 ZEROP (f_zerop.htm)
(list
 (ck "12.2 ZEROP 1" (zerop 0) t)
 (ck "12.2 ZEROP 2" (zerop 0.0) t)
 (ck "12.2 ZEROP 3" (zerop -0.0) t))
;; CLHS 13.2 CHAR-CODE (f_char_c.htm)
(list
 (ck "13.2 CHAR-CODE 1" (char-code #\a) 97)
 (ck "13.2 CHAR-CODE 2" (char-code #\A) 65))
;; CLHS 13.2 CHARACTERP (f_chp.htm)
(list
 (ck "13.2 CHARACTERP 1" (characterp #\a) t)
 (ck "13.2 CHARACTERP 2" (characterp "a") nil))
;; CLHS 13.2 CODE-CHAR (f_code_c.htm)
(list
 (ck "13.2 CODE-CHAR 1" (code-char 97) #\a))
;; CLHS 14.2 APPEND (f_append.htm)
(list
 (ck "14.2 APPEND 1" (append '(1) '(2) '(3 4)) '(1 2 3 4))
 (ck "14.2 APPEND 2" (append nil nil) nil)
 (ck "14.2 APPEND 3" (append) nil)
 (ck "14.2 APPEND 4" (ignore-errors (append '(1 . 2) '(3))) 'nothing))
;; CLHS 14.2 ASSOC (f_assocc.htm)
(list
 (ck "14.2 ASSOC 1" (assoc 'b '((a 1) (b 2))) '(b 2))
 (ck "14.2 ASSOC 2" (assoc "b" '(("b" 1))) nil)
 (ck "14.2 ASSOC 3" (assoc 'x '((a 1) nil (x 2))) '(x 2))
 (ck "14.2 ASSOC 4" (assoc 'x nil) nil))
;; CLHS 14.2 ATOM (a_atom.htm)
(list
 (ck "14.2 ATOM 1" (atom 5) t)
 (ck "14.2 ATOM 2" (atom nil) t)
 (ck "14.2 ATOM 3" (atom '(1)) nil))
;; CLHS 14.2 CAAR (f_car_c.htm)
(list
 (ck "14.2 CAAR 1" (caar '((1) 2)) 1))
;; CLHS 14.2 CADAR (f_car_c.htm)
(list
 (ck "14.2 CADAR 1" (cadar '((1 2) 3)) 2))
;; CLHS 14.2 CADDR (f_car_c.htm)
(list
 (ck "14.2 CADDR 1" (caddr '(1 2 3)) 3))
;; CLHS 14.2 CADR (f_car_c.htm)
(list
 (ck "14.2 CADR 1" (cadr '(1 2 3)) 2))
;; CLHS 14.2 CAR (f_car_c.htm)
(list
 (ck "14.2 CAR 1" (car '(1 2)) 1)
 (ck "14.2 CAR 2" (car nil) nil)
 (ck "14.2 CAR 3" (ignore-errors (car 5)) 'nothing))
;; CLHS 14.2 CDDDR (f_car_c.htm)
(list
 (ck "14.2 CDDDR 1" (cdddr '(1 2 3 4)) '(4)))
;; CLHS 14.2 CDDR (f_car_c.htm)
(list
 (ck "14.2 CDDR 1" (cddr '(1 2 3)) '(3)))
;; CLHS 14.2 CDR (f_car_c.htm)
(list
 (ck "14.2 CDR 1" (cdr '(1 2)) '(2))
 (ck "14.2 CDR 2" (cdr nil) nil))
;; CLHS 14.2 CONS (a_cons.htm)
(list
 (ck "14.2 CONS 1" (cons 1 2) '(1 . 2))
 (ck "14.2 CONS 2" (cons 1 '(2)) '(1 2)))
;; CLHS 14.2 CONSP (f_consp.htm)
(list
 (ck "14.2 CONSP 1" (consp nil) nil)
 (ck "14.2 CONSP 2" (consp '(1)) t))
;; CLHS 14.2 COPY-LIST (f_cp_lis.htm)
(list
 (ck "14.2 COPY-LIST 1" (copy-list '(1 2 3)) '(1 2 3))
 (ck "14.2 COPY-LIST 2" (let ((a '(1 2))) (eq a (copy-list a))) nil))
;; CLHS 14.2 FIRST (f_firstc.htm)
(list
 (ck "14.2 FIRST 1" (first '(1 2)) 1))
;; CLHS 14.2 LIST (a_list.htm)
(list
 (ck "14.2 LIST 1" (list 1 2 3) '(1 2 3))
 (ck "14.2 LIST 2" (list) nil))
;; CLHS 14.2 LISTP (f_listp.htm)
(list
 (ck "14.2 LISTP 1" (listp nil) t)
 (ck "14.2 LISTP 2" (listp "a") nil))
;; CLHS 14.2 MAPCAN (f_mapc_.htm)
(list
 (ck "14.2 MAPCAN 1" (mapcan #'list '(1 2)) '(1 2))
 (ck "14.2 MAPCAN 2" (mapcan (lambda (x) (list x x)) '(1 2)) '(1 1 2 2)))
;; CLHS 14.2 MAPCAR (f_mapc_.htm)
(list
 (ck "14.2 MAPCAR 1" (mapcar #'1+ '(1 2 3)) '(2 3 4))
 (ck "14.2 MAPCAR 2" (mapcar #'+ '(1 2 3) '(10 20)) '(11 22))
 (ck "14.2 MAPCAR 3" (mapcar #'car '((1) (2))) '(1 2))
 (ck "14.2 MAPCAR 4" (mapcar (lambda (x) (* x x)) '(1 2 3)) '(1 4 9)))
;; CLHS 14.2 MAPCON (f_mapc_.htm)
(list
 (ck "14.2 MAPCON 1" (mapcon #'copy-list '(1 2 3)) '(1 2 3 2 3 3)))
;; CLHS 14.2 MEMBER (a_member.htm)
(list
 (ck "14.2 MEMBER 1" (member 2 '(1 2 3)) '(2 3))
 (ck "14.2 MEMBER 2" (member "a" '("a")) nil)
 (ck "14.2 MEMBER 3" (member 5 '(1 2 3)) nil)
 (ck "14.2 MEMBER 4" (member 2.0 '(1 2.0 3)) '(2.0 3)))
;; CLHS 14.2 NTH (f_nth.htm)
(list
 (ck "14.2 NTH 1" (nth 1 '(a b c)) 'b)
 (ck "14.2 NTH 2" (nth 5 '(a)) nil)
 (ck "14.2 NTH 3" (ignore-errors (nth -1 '(a))) 'nothing))
;; CLHS 14.2 NULL (a_null.htm)
(list
 (ck "14.2 NULL 1" (null '()) t)
 (ck "14.2 NULL 2" (null 0) nil))
;; CLHS 14.2 REST (f_rest.htm)
(list
 (ck "14.2 REST 1" (rest '(1 2 3)) '(2 3)))
;; CLHS 15.2 AREF (f_aref.htm)
(list
 (ck "15.2 AREF 1" (aref (make-array 3 :initial-element 0) 1) 0)
 (ck "15.2 AREF 2" (ignore-errors (aref (make-array 3) 3)) 'nothing)
 (ck "15.2 AREF 3" (aref (make-array '(2 2) :initial-element 9) 1 1) 9))
;; CLHS 15.2 ARRAY-DIMENSIONS (f_ar_d_1.htm)
(list
 (ck "15.2 ARRAY-DIMENSIONS 1" (array-dimensions (make-array '(2 3))) '(2 3))
 (ck "15.2 ARRAY-DIMENSIONS 2" (array-dimensions (make-array 4)) '(4)))
;; CLHS 15.2 ARRAYP (f_arrayp.htm)
(list
 (ck "15.2 ARRAYP 1" (arrayp (make-array 2)) t)
 (ck "15.2 ARRAYP 2" (arrayp '(1)) nil))
;; CLHS 15.2 MAKE-ARRAY (f_mk_ar.htm)
(list
 (ck "15.2 MAKE-ARRAY 1" (ignore-errors (make-array -1)) 'nothing))
;; CLHS 16.2 CHAR (f_char_.htm)
(list
 (ck "16.2 CHAR 1" (char "abc" 1) #\b)
 (ck "16.2 CHAR 2" (ignore-errors (char "abc" 3)) 'nothing))
;; CLHS 16.2 STRING (a_string.htm)
(list
 (ck "16.2 STRING 1" (string "abc") "abc")
 (ck "16.2 STRING 2" (string #\a) "a"))
;; CLHS 16.2 STRING/= (f_stgeq_.htm)
(list
 (ck "16.2 STRING/= 1" (string/= "a" "b") 0)
 (ck "16.2 STRING/= 2" (string/= "a" "a") nil))
;; CLHS 16.2 STRING< (f_stgeq_.htm)
(list
 (ck "16.2 STRING< 1" (string< "a" "b") 0)
 (ck "16.2 STRING< 2" (string< "b" "a") nil)
 (ck "16.2 STRING< 3" (string< "abc" "abd") 2)
 (ck "16.2 STRING< 4" (string< "a" "a") nil)
 (ck "16.2 STRING< 5" (string< "ab" "abc") 2))
;; CLHS 16.2 STRING<= (f_stgeq_.htm)
(list
 (ck "16.2 STRING<= 1" (string<= "a" "a") 1))
;; CLHS 16.2 STRING= (f_stgeq_.htm)
(list
 (ck "16.2 STRING= 1" (string= "abc" "abc") t)
 (ck "16.2 STRING= 2" (string= "abc" "abd") nil)
 (ck "16.2 STRING= 3" (string= "abc" "ABC") nil))
;; CLHS 16.2 STRING> (f_stgeq_.htm)
(list
 (ck "16.2 STRING> 1" (string> "b" "a") 0)
 (ck "16.2 STRING> 2" (string> "a" "b") nil))
;; CLHS 16.2 STRING>= (f_stgeq_.htm)
(list
 (ck "16.2 STRING>= 1" (string>= "b" "a") 0))
;; CLHS 16.2 STRINGP (f_stgp.htm)
(list
 (ck "16.2 STRINGP 1" (stringp "a") t)
 (ck "16.2 STRINGP 2" (stringp 'a) nil))
;; CLHS 17.3 CONCATENATE (f_concat.htm)
(list
 (ck "17.3 CONCATENATE 1" (concatenate 'string "a" "b") "ab")
 (ck "17.3 CONCATENATE 2" (ignore-errors (concatenate 'array '(1) '(2))) 'nothing)
 (ck "17.3 CONCATENATE 3" (concatenate 'string) ""))
;; CLHS 17.3 LENGTH (f_length.htm)
(list
 (ck "17.3 LENGTH 1" (length "abc") 3)
 (ck "17.3 LENGTH 2" (length '(1 2)) 2)
 (ck "17.3 LENGTH 3" (length nil) 0)
 (ck "17.3 LENGTH 4" (length (make-array 3)) 3)
 (ck "17.3 LENGTH 5" (ignore-errors (length 5)) 'nothing))
;; CLHS 17.3 REVERSE (f_revers.htm)
(list
 (ck "17.3 REVERSE 1" (reverse '(1 2 3)) '(3 2 1))
 (ck "17.3 REVERSE 2" (reverse nil) nil))
;; CLHS 17.3 SEARCH (f_search.htm)
(list
 (ck "17.3 SEARCH 1" (search "b" "abc") 1)
 (ck "17.3 SEARCH 2" (search "x" "abc") nil)
 (ck "17.3 SEARCH 3" (search '(2) '(1 2 3)) 1)
 (ck "17.3 SEARCH 4" (search "" "abc") 0))
;; CLHS 17.3 SORT (f_sort_.htm)
(list
 (ck "17.3 SORT 1" (sort (list 3 1 2) #'<) '(1 2 3))
 (ck "17.3 SORT 2" (sort (list 3 1 2) #'>) '(3 2 1))
 (ck "17.3 SORT 3" (sort '(3 1 2) #'<) '(1 2 3))
 (ck "17.3 SORT 4" (sort (list "b" "a") #'string<) '("a" "b"))
 (ck "17.3 SORT 5" (sort (list '(1 b) '(1 a) '(0 c)) (lambda (x y) (< (car x) (car y)))) '((0 c) (1 b) (1 a)))
 (ck "17.3 SORT 6" (sort nil #'<) nil))
;; CLHS 17.3 SUBSEQ (f_subseq.htm)
(list
 (ck "17.3 SUBSEQ 1" (subseq "abc" 1 2) "b")
 (ck "17.3 SUBSEQ 2" (subseq "abc" 1) "bc")
 (ck "17.3 SUBSEQ 3" (subseq '(1 2 3) 1) '(2 3))
 (ck "17.3 SUBSEQ 4" (subseq '(1 2 3) 1 2) '(2))
 (ck "17.3 SUBSEQ 5" (ignore-errors (subseq "abc" 2 5)) 'nothing)
 (ck "17.3 SUBSEQ 6" (ignore-errors (subseq "abc" 2 1)) 'nothing)
 (ck "17.3 SUBSEQ 7" (ignore-errors (subseq "abc" -1)) 'nothing))
;; CLHS 21.2 STREAMP (f_stmp.htm)
(list
 (ck "21.2 STREAMP 1" (streamp 5) nil))
;; CLHS 23.2 READ-FROM-STRING (f_rd_fro.htm)
(list
 (ck "23.2 READ-FROM-STRING 1" (read-from-string "(1 2)") '(1 2))
 (ck "23.2 READ-FROM-STRING 2" (read-from-string "abc def") 'abc)
 (ck "23.2 READ-FROM-STRING 3" (ignore-errors (read-from-string "")) 'nothing)
 (ck "23.2 READ-FROM-STRING 4" (read-from-string "  42  ") 42)
 (ck "23.2 READ-FROM-STRING 5" (read-from-string "\"hi\"") "hi"))

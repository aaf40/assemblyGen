CC = gcc
CFLAGS = -I./src -std=gnu99 -g

LEX=lex

YACC=yacc
YFLAGS=-v -d -b obj/y
GRAMMAR=parser.y

obj/mcc: obj/lex.yy.o obj/y.tab.o obj/tree.o obj/driver.o obj/strtab.o obj/codegen.o | obj
	$(CC) $(CFLAGS) -o $@ $^ -ll

obj/y.tab.o: obj/y.tab.c | obj
	$(CC) $(CFLAGS) -c $< -o $@

obj/lex.yy.o: obj/lex.yy.c | obj
	$(CC) $(CFLAGS) -c $< -o $@

obj/y.tab.h obj/y.tab.c: src/$(GRAMMAR)  src/tree.h | obj
	$(YACC) $(YFLAGS) $<

obj/lex.yy.c: src/scanner.l obj/y.tab.h | obj
	$(LEX) -o $@ $<

obj/tree.o: src/tree.c src/tree.h | obj
	$(CC)  $(CFLAGS) -c $< -o $@

obj/driver.o: src/driver.c src/tree.h obj/y.tab.h | obj
	$(CC)  $(CFLAGS) -c $< -o $@

obj/strtab.o: src/strtab.c src/strtab.h obj/y.tab.h | obj
	$(CC)  $(CFLAGS) -c $< -o $@

obj/codegen.o: src/codegen.c src/codegen.h | obj
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean test objdir

obj:
	@mkdir -p obj

clean:
	@rm -rf obj
	@rm -f lex.yy.* *.o *~ scanner

test: obj/mcc
	@python ./test/testParser.py

testPar: obj/mcc
	@python ./test/testPar.py

testSym: obj/mcc
	python3 test/testSymTab.py

testGlobl:
	 make clean;make;clear;./obj/mcc test/cases/globlIntOut.mC > out.s; python test/compare.py ./test/exp/globlIntOut.exp out.s

testLocl:
	 make clean;make;clear;./obj/mcc test/cases/loclIntOut.mC > out.s; python test/compare.py ./test/exp/loclIntOut.exp out.s

testArith:
	 make clean;make;clear;./obj/mcc test/cases/testArith.mC > out.s; python test/compare.py ./test/exp/testArith.exp out.s

testCond:
	 make clean;make;clear;./obj/mcc test/cases/testCond.mC > out.s; python test/compare.py ./test/exp/testCond.exp out.s

testFunc:
	 make clean;make;clear;./obj/mcc test/cases/testFunc.mC > out.s; python test/compare.py ./test/exp/testFunc.exp out.s

testLoop:
	 make clean;make;clear;./obj/mcc test/cases/testLoop.mC > out.s; python test/compare.py ./test/exp/testLoop.exp out.s

testErr1:
	 	make clean;make;clear;./obj/mcc test/cases/errMulDeclFunc.mC > out.s; python test/compare.py ./test/exp/errMulDeclFunc.exp out.s

testErr2:
	 make clean;make;clear;./obj/mcc test/cases/errMulDeclVar.mC > out.s; python test/compare.py ./test/exp/errMulDeclVar.exp out.s

testErr3:
	 	make clean;make;clear;./obj/mcc test/cases/errUnDeclFunc.mC > out.s; python test/compare.py ./test/exp/errUnDeclFunc.exp out.s
		
testErr4:
	 	make clean;make;clear;./obj/mcc test/cases/errUnDeclVar.mC > out.s; python test/compare.py ./test/exp/errUnDeclVar.exp out.s


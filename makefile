CC = gcc
CFLAGS = -I./src -std=gnu99 -g

LEX=lex

YACC=yacc
YFLAGS=-v -d -b obj/y
GRAMMAR=parser.y

obj/mcc: obj/lex.yy.o obj/y.tab.o obj/tree.o obj/driver.o obj/strtab.o | obj
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

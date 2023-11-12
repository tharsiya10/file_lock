BIN_FOLDER := ./bin
LIBNAME:=rl_lock_library
TESTNAME:=rl_lock_test

LIB_NAME_BIN := $(addsuffix .a, $(addprefix lib, $(LIBNAME)))

$(BIN_FOLDER)/$(TESTNAME): $(BIN_FOLDER)/$(LIB_NAME_BIN) ./test/test.c
	gcc -o $@ -I include ./test/test.c -L$(BIN_FOLDER)/ -l$(LIBNAME) -pthread -lrt

$(BIN_FOLDER)/$(LIB_NAME_BIN): $(BIN_FOLDER)/rl_lock_library.o
	ar ruv $@ $(BIN_FOLDER)/rl_lock_library.o

$(BIN_FOLDER)/rl_lock_library.o: src/rl_lock_library.c include/rl_lock_library.h
	gcc -c -fPIC -I include -o $(BIN_FOLDER)/rl_lock_library.o src/rl_lock_library.c -Wall


all: $(BIN_FOLDER)/$(TESTNAME)

clean:
	rm -rf $(BIN_FOLDER)/*

args ?=
GCC ?= g++
PNAME = run.out

build:
	$(GCC) ./src/*.cpp -I ./src/include \
		-static-libgcc -static-libstdc++ \
		-std=c++20 -pthread -o ./bin/$(PNAME)
run:
	./bin/$(PNAME) $(args)
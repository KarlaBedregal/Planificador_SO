# Makefile - Semana 4 Sistemas Operativos
# Se usa '>' como prefijo de receta en vez de TAB, para que no se rompa al copiar.
.RECIPEPREFIX := >
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
NUCLEO   := src/planificador.cpp src/planificador.h

all: bin/parte_a bin/parte_b bin/practica bin/tests

bin:
> mkdir -p bin

bin/parte_a: src/parte_a_simulador.cpp $(NUCLEO) | bin
> $(CXX) $(CXXFLAGS) -o $@ src/parte_a_simulador.cpp src/planificador.cpp

bin/parte_b: src/parte_b_productor_consumidor.cpp | bin
> $(CXX) $(CXXFLAGS) -o $@ src/parte_b_productor_consumidor.cpp

bin/practica: src/practica_planificador_ipc.cpp $(NUCLEO) | bin
> $(CXX) $(CXXFLAGS) -o $@ src/practica_planificador_ipc.cpp src/planificador.cpp

bin/tests: tests/test_planificador.cpp $(NUCLEO) | bin
> $(CXX) $(CXXFLAGS) -o $@ tests/test_planificador.cpp src/planificador.cpp

test: bin/tests
> ./bin/tests | tee salidas/pruebas_unitarias.txt

run: all
> mkdir -p salidas
> ./bin/tests | tee salidas/pruebas_unitarias.txt
> ./bin/parte_a
> ./bin/parte_b
> ./bin/practica --probar-error

clean:
> rm -rf bin

.PHONY: all test run clean
# Planificador de procesos con IPC (C++)

Sistemas Operativos · Karla Miluska Bedregal Coaguila

Simulador de planificación de CPU (FCFS, SJF, Prioridad y Round Robin) y comunicación entre procesos con pipes POSIX (`fork`, `pipe`, `poll`, `waitpid`). 

## Requisitos

Linux o WSL en Windows (se usan llamadas POSIX), `g++` con soporte de C++17 y `make`.

## Compilar y ejecutar

```bash
make            # compila todo en bin/
make run        # pruebas + Parte A + Parte B + práctica (genera salidas/)
```

O cada programa por separado:

```bash
./bin/tests                                   # pruebas unitarias
./bin/parte_a                                 # Parte A: FCFS y RR (quantum 2 y 4)
./bin/parte_a entradas/carga1_cpu.txt 1 2 4   # otra carga u otros quantum
./bin/parte_b                                 # Parte B: productor/consumidor con pipe
./bin/practica --probar-error                 # práctica: coordinador + 3 trabajadores
./bin/practica --cargas entradas/carga2_interactiva.txt --quantums 1 3 --trabajadores 4 --retardo 0
```

Sin `make`: `g++ -std=c++17 -O2 -o bin/parte_a src/parte_a_simulador.cpp src/planificador.cpp` (igual para los demás).

## Estructura

```
src/planificador.h / .cpp           núcleo: algoritmos, métricas, formato
src/parte_a_simulador.cpp           Parte A del laboratorio
src/parte_b_productor_consumidor.cpp Parte B del laboratorio
src/practica_planificador_ipc.cpp   práctica domiciliaria
tests/test_planificador.cpp         pruebas con valores calculados a mano
entradas/                           archivos de prueba
salidas/                            resultados generados
```

## Formato de entrada

Una línea por proceso: `PID llegada ráfaga prioridad` (prioridad opcional; número menor = más alta). Se permiten comentarios con `#`.

```
P1 0 5 2
P2 1 3 1
P3 2 1 3
```
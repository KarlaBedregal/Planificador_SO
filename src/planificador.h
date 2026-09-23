// planificador.h
// Nucleo del simulador: estructuras, algoritmos (FCFS, SJF, Prioridad, Round Robin),
// metricas y formato de salida. Lo usan la Parte A, la practica y las pruebas.
#pragma once

#include <string>
#include <vector>

// Un proceso leido del archivo: PID llegada rafaga prioridad
struct Proceso {
    std::string pid;
    int llegada = 0;
    int rafaga = 0;
    int prioridad = 0;   // numero menor = prioridad mas alta
    int idx = 0;         // orden en el archivo (para desempatar)
};

// Un tramo de la linea temporal: [inicio, fin) ejecuta pid ("IDLE" = CPU ociosa)
struct Tramo {
    int inicio;
    int fin;
    std::string pid;
};

// Metricas de un proceso
struct Fila {
    std::string pid;
    int llegada, rafaga, prioridad;
    int inicio, fin;
    int retorno, espera, respuesta;
};

// Resultado completo de simular un algoritmo sobre una carga
struct Resultado {
    std::string algoritmo;   // FCFS, SJF, PRIORIDAD, RR
    int quantum = 0;         // solo para RR
    std::string carga;       // nombre de la carga (lo llena la practica)
    std::vector<Tramo> linea;
    std::vector<Fila> filas;
    double promRetorno = 0, promEspera = 0, promRespuesta = 0;
    int maxEspera = 0;
    int cambiosContexto = 0;
    double throughput = 0;   // procesos por unidad de tiempo
    double utilizacion = 0;  // % de tiempo con CPU ocupada
};

// Lectura del archivo de entrada. Lanza std::runtime_error si hay errores.
std::vector<Proceso> leerProcesos(const std::string& ruta);

// Algoritmos
Resultado fcfs(const std::vector<Proceso>& procesos);
Resultado sjf(const std::vector<Proceso>& procesos);          // no expropiativo
Resultado prioridad(const std::vector<Proceso>& procesos);    // no expropiativo (extra)
Resultado roundRobin(const std::vector<Proceso>& procesos, int quantum);

// Ejecuta un algoritmo por nombre. Lanza std::invalid_argument si no existe.
Resultado ejecutar(const std::string& algoritmo, const std::vector<Proceso>& procesos, int quantum);

// Calcula promedios, espera maxima, cambios de contexto, throughput y utilizacion
// a partir de r.linea y r.filas.
void completarMetricas(Resultado& r);

// Utilidades de salida
std::string nombreAlgoritmo(const Resultado& r);   // "FCFS" o "RR (q=2)"
std::string lineaComoTexto(const Resultado& r);    // "[0-5 P1] [5-8 P2] ..."
std::string formatear(const Resultado& r);         // bloque completo con Gantt y tabla
// test_planificador.cpp
// Pruebas con valores calculados a mano. Ejecutar: ./bin/tests  (o: make test)
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/planificador.h"

using namespace std;

static int pasadas = 0, fallidas = 0;

static void verificar(bool condicion, const string& nombre) {
    if (condicion) {
        pasadas++;
        cout << "  [OK]    " << nombre << "\n";
    } else {
        fallidas++;
        cout << "  [FALLA] " << nombre << "\n";
    }
}

static vector<int> col(const Resultado& r, int Fila::*campo) {
    vector<int> v;
    for (const Fila& f : r.filas) v.push_back(f.*campo);
    return v;
}

// P1 llega 0 raf 5 prio 2 | P2 llega 1 raf 3 prio 1 | P3 llega 2 raf 1 prio 3
static vector<Proceso> caso() {
    return {{"P1", 0, 5, 2, 0}, {"P2", 1, 3, 1, 1}, {"P3", 2, 1, 3, 2}};
}

int main() {
    cout << "Pruebas del planificador\n";

    {   // FCFS
        Resultado r = fcfs(caso());
        verificar(lineaComoTexto(r) == "[0-5 P1] [5-8 P2] [8-9 P3]", "FCFS linea temporal");
        verificar(col(r, &Fila::retorno) == vector<int>{5, 7, 7}, "FCFS retorno");
        verificar(col(r, &Fila::espera) == vector<int>{0, 4, 6}, "FCFS espera");
        verificar(col(r, &Fila::respuesta) == vector<int>{0, 4, 6}, "FCFS respuesta");
    }
    {   // SJF no expropiativo: en t=0 solo esta P1; en t=5 gana P3 (rafaga 1)
        Resultado r = sjf(caso());
        verificar(lineaComoTexto(r) == "[0-5 P1] [5-6 P3] [6-9 P2]", "SJF linea temporal");
        verificar(col(r, &Fila::espera) == vector<int>{0, 5, 3}, "SJF espera");
        verificar(col(r, &Fila::retorno) == vector<int>{5, 8, 4}, "SJF retorno");
    }
    {   // Prioridad: en t=5 gana P2 (prioridad 1)
        Resultado r = prioridad(caso());
        verificar(lineaComoTexto(r) == "[0-5 P1] [5-8 P2] [8-9 P3]", "Prioridad linea temporal");
    }
    {   // RR q=2
        Resultado r = roundRobin(caso(), 2);
        verificar(lineaComoTexto(r) == "[0-2 P1] [2-4 P2] [4-5 P3] [5-7 P1] [7-8 P2] [8-9 P1]",
                  "RR q=2 linea temporal");
        verificar(col(r, &Fila::retorno) == vector<int>{9, 7, 3}, "RR q=2 retorno");
        verificar(col(r, &Fila::espera) == vector<int>{4, 4, 2}, "RR q=2 espera");
        verificar(col(r, &Fila::respuesta) == vector<int>{0, 1, 2}, "RR q=2 respuesta");
        verificar(r.cambiosContexto == 5, "RR q=2 cambios de contexto");
    }
    {   // RR q=4
        Resultado r = roundRobin(caso(), 4);
        verificar(lineaComoTexto(r) == "[0-4 P1] [4-7 P2] [7-8 P3] [8-9 P1]", "RR q=4 linea temporal");
        verificar(col(r, &Fila::espera) == vector<int>{4, 3, 5}, "RR q=4 espera");
    }
    {   // Quantum mayor que toda rafaga -> igual a FCFS
        verificar(lineaComoTexto(roundRobin(caso(), 100)) == lineaComoTexto(fcfs(caso())),
                  "RR con quantum grande = FCFS");
    }
    {   // CPU ociosa: P2 llega cuando P1 ya termino
        vector<Proceso> v{{"P1", 0, 2, 1, 0}, {"P2", 5, 3, 1, 1}};
        for (const Resultado& r : {fcfs(v), sjf(v), roundRobin(v, 2)}) {
            verificar(lineaComoTexto(r) == "[0-2 P1] [2-5 IDLE] [5-8 P2]",
                      nombreAlgoritmo(r) + " con CPU ociosa");
            verificar(r.utilizacion > 62.4 && r.utilizacion < 62.6,
                      nombreAlgoritmo(r) + " utilizacion 62.5%");
        }
    }
    {   // espera = retorno - rafaga siempre
        bool ok = true;
        for (const Resultado& r : {fcfs(caso()), sjf(caso()), roundRobin(caso(), 1)})
            for (const Fila& f : r.filas) ok = ok && (f.espera == f.retorno - f.rafaga);
        verificar(ok, "espera = retorno - rafaga en todos los algoritmos");
    }
    {   // Errores
        bool lanzo = false;
        try { ejecutar("LOTERIA", caso(), 0); } catch (const invalid_argument&) { lanzo = true; }
        verificar(lanzo, "algoritmo desconocido lanza excepcion");
        lanzo = false;
        try { roundRobin(caso(), 0); } catch (const invalid_argument&) { lanzo = true; }
        verificar(lanzo, "quantum 0 lanza excepcion");
    }
    {   // Lectura de archivo
        vector<Proceso> v = leerProcesos("entradas/caso_mano.txt");
        verificar(v.size() == 3 && v[1].pid == "P2" && v[1].rafaga == 3, "lectura de caso_mano.txt");
    }

    cout << "\nResultado: " << pasadas << " pasadas, " << fallidas << " fallidas\n";
    return fallidas == 0 ? 0 : 1;
}
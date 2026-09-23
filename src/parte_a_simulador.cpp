// parte_a_simulador.cpp
// Parte A del laboratorio (diapositiva 18): FCFS y Round Robin.
//  1) Valida contra un caso pequeno calculado a mano (entradas/caso_mano.txt).
//  2) Corre FCFS y RR con varios quantum sobre una carga mas grande.
//
// Uso:  ./bin/parte_a                                   (carga1_cpu, quantum 2 y 4)
//       ./bin/parte_a entradas/carga1_cpu.txt 1 2 4      (archivo y quantum a elegir)
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include "planificador.h"

using namespace std;

struct Esperado {
    string linea;
    vector<int> retorno, espera, respuesta;
};

static vector<int> columna(const Resultado& r, int Fila::*campo) {
    vector<int> v;
    for (const Fila& f : r.filas) v.push_back(f.*campo);
    return v;
}

static string vecTexto(const vector<int>& v) {
    string s = "[";
    for (size_t i = 0; i < v.size(); i++) s += (i ? "," : "") + to_string(v[i]);
    return s + "]";
}

static bool validar(const Resultado& r, const Esperado& e, const string& nombre, ostream& out) {
    bool ok = true;
    auto comparar = [&](const string& que, const string& esp, const string& obt) {
        if (esp != obt) {
            ok = false;
            out << "  [FALLA] " << nombre << " " << que << ": esperado " << esp
                << ", obtenido " << obt << "\n";
        }
    };
    comparar("linea", e.linea, lineaComoTexto(r));
    comparar("retorno", vecTexto(e.retorno), vecTexto(columna(r, &Fila::retorno)));
    comparar("espera", vecTexto(e.espera), vecTexto(columna(r, &Fila::espera)));
    comparar("respuesta", vecTexto(e.respuesta), vecTexto(columna(r, &Fila::respuesta)));
    out << "  [" << (ok ? "OK" : "ERROR") << "] " << nombre << " coincide con el calculo a mano\n";
    return ok;
}

int main(int argc, char* argv[]) {
    ostringstream out;
    out << fixed << setprecision(2);
    bool ok = true;

    try {
        // ---------------- 1) Validacion con el caso hecho a mano ----------------
        // P1 llega 0 rafaga 5 | P2 llega 1 rafaga 3 | P3 llega 2 rafaga 1
        vector<Proceso> caso = leerProcesos("entradas/caso_mano.txt");
        Esperado espFcfs{"[0-5 P1] [5-8 P2] [8-9 P3]", {5, 7, 7}, {0, 4, 6}, {0, 4, 6}};
        Esperado espRr2{"[0-2 P1] [2-4 P2] [4-5 P3] [5-7 P1] [7-8 P2] [8-9 P1]",
                        {9, 7, 3}, {4, 4, 2}, {0, 1, 2}};

        out << "##### VALIDACION CON CASO CALCULADO A MANO (entradas/caso_mano.txt) #####\n";
        Resultado rF = fcfs(caso);
        Resultado rR = roundRobin(caso, 2);
        out << formatear(rF) << formatear(rR);
        bool ok1 = validar(rF, espFcfs, "FCFS", out);
        bool ok2 = validar(rR, espRr2, "RR q=2", out);
        ok = ok1 && ok2;
        out << "Validacion: " << (ok ? "CORRECTA" : "CON ERRORES") << "\n";

        // ---------------- 2) Carga mas grande con varios quantum ----------------
        string ruta = argc > 1 ? argv[1] : "entradas/carga1_cpu.txt";
        vector<int> quantums;
        for (int k = 2; k < argc; k++) quantums.push_back(stoi(argv[k]));
        if (quantums.empty()) quantums = {2, 4};

        vector<Proceso> procesos = leerProcesos(ruta);
        out << "\n##### CARGA: " << ruta << " | quantum probados:";
        for (int q : quantums) out << " " << q;
        out << " #####\n";

        vector<Resultado> resultados{fcfs(procesos)};
        for (int q : quantums) resultados.push_back(roundRobin(procesos, q));
        for (const Resultado& r : resultados) out << formatear(r) << "\n";

        out << "Resumen:\n";
        for (const Resultado& r : resultados) {
            out << "  " << left << setw(10) << nombreAlgoritmo(r) << right
                << " retorno " << setw(6) << r.promRetorno << " | espera " << setw(6) << r.promEspera
                << " | respuesta " << setw(6) << r.promRespuesta << " | cambios "
                << setw(3) << r.cambiosContexto << "\n";
        }
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << "\n";
        return 2;
    }

    cout << out.str();
    filesystem::create_directories("salidas");
    ofstream("salidas/parte_a_salida.txt") << out.str();
    cout << "\nSalida guardada en salidas/parte_a_salida.txt\n";
    return ok ? 0 : 1;
}
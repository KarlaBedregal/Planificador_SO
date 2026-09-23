// planificador.cpp
#include "planificador.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

using namespace std;

// ---------------------------------------------------------------------------
// Lectura
// ---------------------------------------------------------------------------
vector<Proceso> leerProcesos(const string& ruta) {
    ifstream archivo(ruta);
    if (!archivo) throw runtime_error("no se pudo abrir " + ruta);

    vector<Proceso> procesos;
    set<string> vistos;
    string linea;
    int numLinea = 0;
    while (getline(archivo, linea)) {
        numLinea++;
        size_t pos = linea.find('#');                 // comentarios
        if (pos != string::npos) linea = linea.substr(0, pos);
        replace(linea.begin(), linea.end(), ',', ' '); // acepta comas
        istringstream ss(linea);

        Proceso p;
        if (!(ss >> p.pid)) continue;                 // linea vacia
        string mayus = p.pid;
        transform(mayus.begin(), mayus.end(), mayus.begin(), ::toupper);
        if (mayus == "PID") continue;                 // cabecera opcional

        string donde = ruta + ":" + to_string(numLinea) + ": ";
        if (!(ss >> p.llegada >> p.rafaga))
            throw runtime_error(donde + "se esperan al menos PID llegada rafaga");
        if (!(ss >> p.prioridad)) p.prioridad = 0;     // prioridad opcional
        if (p.llegada < 0 || p.rafaga <= 0)
            throw runtime_error(donde + "llegada debe ser >= 0 y rafaga > 0");
        if (!vistos.insert(p.pid).second)
            throw runtime_error(donde + "PID repetido " + p.pid);

        p.idx = static_cast<int>(procesos.size());
        procesos.push_back(p);
    }
    if (procesos.empty()) throw runtime_error(ruta + ": archivo sin procesos");
    return procesos;
}

// ---------------------------------------------------------------------------
// Auxiliares
// ---------------------------------------------------------------------------
// Copia los procesos asegurando que idx sea su posicion (desempate estable)
static vector<Proceso> conIndice(const vector<Proceso>& entrada) {
    vector<Proceso> v = entrada;
    for (size_t i = 0; i < v.size(); i++) v[i].idx = static_cast<int>(i);
    return v;
}

static bool porLlegada(const Proceso& a, const Proceso& b) {
    return tie(a.llegada, a.idx) < tie(b.llegada, b.idx);
}

// Une tramos consecutivos del mismo proceso
static vector<Tramo> fusionar(const vector<Tramo>& linea) {
    vector<Tramo> out;
    for (const Tramo& t : linea) {
        if (!out.empty() && out.back().pid == t.pid && out.back().fin == t.inicio)
            out.back().fin = t.fin;
        else
            out.push_back(t);
    }
    return out;
}

static Resultado armarResultado(const string& nombre, int quantum, vector<Proceso> procesos,
                                const vector<Tramo>& linea, map<string, int>& inicio,
                                map<string, int>& fin) {
    Resultado r;
    r.algoritmo = nombre;
    r.quantum = quantum;
    r.linea = fusionar(linea);
    sort(procesos.begin(), procesos.end(),
         [](const Proceso& a, const Proceso& b) { return a.idx < b.idx; });
    for (const Proceso& p : procesos) {
        Fila f;
        f.pid = p.pid;
        f.llegada = p.llegada;
        f.rafaga = p.rafaga;
        f.prioridad = p.prioridad;
        f.inicio = inicio[p.pid];
        f.fin = fin[p.pid];
        f.retorno = f.fin - f.llegada;             // finalizacion - llegada
        f.espera = f.retorno - f.rafaga;           // tiempo en cola de listos
        f.respuesta = f.inicio - f.llegada;        // primera CPU - llegada
        r.filas.push_back(f);
    }
    completarMetricas(r);
    return r;
}

void completarMetricas(Resultado& r) {
    if (r.filas.empty() || r.linea.empty()) return;
    double n = static_cast<double>(r.filas.size());
    double sr = 0, se = 0, sresp = 0;
    r.maxEspera = 0;
    for (const Fila& f : r.filas) {
        sr += f.retorno;
        se += f.espera;
        sresp += f.respuesta;
        r.maxEspera = max(r.maxEspera, f.espera);
    }
    r.promRetorno = sr / n;
    r.promEspera = se / n;
    r.promRespuesta = sresp / n;

    int tramosCpu = 0, ocupado = 0;
    for (const Tramo& t : r.linea) {
        if (t.pid != "IDLE") {
            tramosCpu++;
            ocupado += t.fin - t.inicio;
        }
    }
    r.cambiosContexto = max(tramosCpu - 1, 0);
    int duracion = r.linea.back().fin - r.linea.front().inicio;
    r.throughput = duracion > 0 ? n / duracion : 0;
    r.utilizacion = duracion > 0 ? 100.0 * ocupado / duracion : 0;
}

// ---------------------------------------------------------------------------
// FCFS
// ---------------------------------------------------------------------------
Resultado fcfs(const vector<Proceso>& entrada) {
    vector<Proceso> procs = conIndice(entrada);
    sort(procs.begin(), procs.end(), porLlegada);

    int t = 0;
    vector<Tramo> linea;
    map<string, int> inicio, fin;
    for (const Proceso& p : procs) {
        if (t < p.llegada) {                       // CPU ociosa hasta que llegue
            linea.push_back({t, p.llegada, "IDLE"});
            t = p.llegada;
        }
        inicio[p.pid] = t;
        linea.push_back({t, t + p.rafaga, p.pid});
        t += p.rafaga;
        fin[p.pid] = t;
    }
    return armarResultado("FCFS", 0, procs, linea, inicio, fin);
}

// ---------------------------------------------------------------------------
// SJF y Prioridad (ambos no expropiativos, solo cambia el criterio)
// ---------------------------------------------------------------------------
using Criterio = function<bool(const Proceso&, const Proceso&)>;

static Resultado noExpropiativo(const vector<Proceso>& entrada, const string& nombre,
                                Criterio menor) {
    vector<Proceso> procs = conIndice(entrada);
    vector<Proceso> pendientes = procs;
    sort(pendientes.begin(), pendientes.end(), porLlegada);

    vector<Proceso> listos;
    size_t i = 0;
    int t = 0;
    vector<Tramo> linea;
    map<string, int> inicio, fin;

    while (i < pendientes.size() || !listos.empty()) {
        while (i < pendientes.size() && pendientes[i].llegada <= t)
            listos.push_back(pendientes[i++]);
        if (listos.empty()) {                      // nadie listo: CPU ociosa
            linea.push_back({t, pendientes[i].llegada, "IDLE"});
            t = pendientes[i].llegada;
            continue;
        }
        auto it = min_element(listos.begin(), listos.end(), menor);
        Proceso p = *it;
        listos.erase(it);
        inicio[p.pid] = t;
        linea.push_back({t, t + p.rafaga, p.pid});
        t += p.rafaga;                             // corre completo, no se expropia
        fin[p.pid] = t;
    }
    return armarResultado(nombre, 0, procs, linea, inicio, fin);
}

Resultado sjf(const vector<Proceso>& procesos) {
    // Rafaga mas corta; empate -> mayor prioridad -> llegada -> orden del archivo
    return noExpropiativo(procesos, "SJF", [](const Proceso& a, const Proceso& b) {
        return tie(a.rafaga, a.prioridad, a.llegada, a.idx) <
               tie(b.rafaga, b.prioridad, b.llegada, b.idx);
    });
}

Resultado prioridad(const vector<Proceso>& procesos) {
    // Prioridad mas alta (numero menor); empate -> llegada -> orden del archivo
    return noExpropiativo(procesos, "PRIORIDAD", [](const Proceso& a, const Proceso& b) {
        return tie(a.prioridad, a.llegada, a.idx) < tie(b.prioridad, b.llegada, b.idx);
    });
}

// ---------------------------------------------------------------------------
// Round Robin
// ---------------------------------------------------------------------------
Resultado roundRobin(const vector<Proceso>& entrada, int quantum) {
    if (quantum <= 0) throw invalid_argument("el quantum debe ser mayor que 0");
    vector<Proceso> procs = conIndice(entrada);
    sort(procs.begin(), procs.end(), porLlegada);

    map<string, int> restante, inicio, fin;
    for (const Proceso& p : procs) restante[p.pid] = p.rafaga;

    deque<Proceso> cola;
    size_t i = 0;
    int t = 0;
    vector<Tramo> linea;

    auto admitir = [&](int hasta) {                // mete a la cola a los que ya llegaron
        while (i < procs.size() && procs[i].llegada <= hasta) cola.push_back(procs[i++]);
    };

    admitir(t);
    while (!cola.empty() || i < procs.size()) {
        if (cola.empty()) {                        // CPU ociosa hasta la proxima llegada
            linea.push_back({t, procs[i].llegada, "IDLE"});
            t = procs[i].llegada;
            admitir(t);
            continue;
        }
        Proceso p = cola.front();
        cola.pop_front();
        if (!inicio.count(p.pid)) inicio[p.pid] = t;   // primera vez en CPU
        int uso = min(quantum, restante[p.pid]);
        linea.push_back({t, t + uso, p.pid});
        t += uso;
        restante[p.pid] -= uso;
        // Convencion: los que llegan durante el quantum (o justo al final)
        // entran a la cola ANTES que el proceso expropiado.
        admitir(t);
        if (restante[p.pid] > 0)
            cola.push_back(p);
        else
            fin[p.pid] = t;
    }
    return armarResultado("RR", quantum, procs, linea, inicio, fin);
}

// ---------------------------------------------------------------------------
// Seleccion por nombre
// ---------------------------------------------------------------------------
Resultado ejecutar(const string& algoritmo, const vector<Proceso>& procesos, int quantum) {
    string a = algoritmo;
    transform(a.begin(), a.end(), a.begin(), ::toupper);
    if (a == "FCFS") return fcfs(procesos);
    if (a == "SJF") return sjf(procesos);
    if (a == "PRIORIDAD") return prioridad(procesos);
    if (a == "RR") return roundRobin(procesos, quantum);
    throw invalid_argument("algoritmo desconocido: " + algoritmo);
}

// ---------------------------------------------------------------------------
// Salida
// ---------------------------------------------------------------------------
string nombreAlgoritmo(const Resultado& r) {
    if (r.algoritmo == "RR") return "RR (q=" + to_string(r.quantum) + ")";
    return r.algoritmo;
}

string lineaComoTexto(const Resultado& r) {
    ostringstream os;
    for (size_t k = 0; k < r.linea.size(); k++) {
        if (k) os << " ";
        os << "[" << r.linea[k].inicio << "-" << r.linea[k].fin << " " << r.linea[k].pid << "]";
    }
    return os.str();
}

string formatear(const Resultado& r) {
    ostringstream os;
    os << fixed << setprecision(2);
    os << "=== " << nombreAlgoritmo(r) << " ===\n";
    os << "Linea temporal (tramos):\n  " << lineaComoTexto(r) << "\n";

    // Diagrama de Gantt por proceso: '#' ejecuta, '.' espera en cola de listos
    int finTotal = r.linea.empty() ? 0 : r.linea.back().fin;
    if (finTotal > 0 && finTotal <= 100) {
        vector<string> corriendo(finTotal);
        for (const Tramo& t : r.linea)
            for (int u = t.inicio; u < t.fin; u++) corriendo[u] = t.pid;
        os << "Gantt ('#' ejecuta, '.' espera):\n";
        os << "  " << setw(5) << "t" << " |";
        for (int u = 0; u < finTotal; u++) os << (u % 10);
        os << "|\n";
        for (const Fila& f : r.filas) {
            os << "  " << setw(5) << f.pid << " |";
            for (int u = 0; u < finTotal; u++) {
                if (corriendo[u] == f.pid) os << '#';
                else if (f.llegada <= u && u < f.fin) os << '.';
                else os << ' ';
            }
            os << "|\n";
        }
    }

    os << "Metricas por proceso:\n";
    os << "  " << setw(5) << "PID" << setw(6) << "Lleg" << setw(5) << "Raf" << setw(5) << "Prio"
       << setw(7) << "Inicio" << setw(5) << "Fin" << setw(8) << "Retorno" << setw(7) << "Espera"
       << setw(6) << "Resp" << "\n";
    for (const Fila& f : r.filas) {
        os << "  " << setw(5) << f.pid << setw(6) << f.llegada << setw(5) << f.rafaga
           << setw(5) << f.prioridad << setw(7) << f.inicio << setw(5) << f.fin << setw(8)
           << f.retorno << setw(7) << f.espera << setw(6) << f.respuesta << "\n";
    }
    os << "  Promedios -> retorno " << r.promRetorno << " | espera " << r.promEspera
       << " | respuesta " << r.promRespuesta << "\n";
    os << "  Espera maxima " << r.maxEspera << " | cambios de contexto " << r.cambiosContexto
       << " | throughput " << setprecision(3) << r.throughput << " proc/u | utilizacion CPU "
       << setprecision(1) << r.utilizacion << "%\n";
    return os.str();
}
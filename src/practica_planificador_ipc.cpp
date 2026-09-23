// practica_planificador_ipc.cpp
// Practica domiciliaria (diapositiva 19): planificador con IPC.
//
// Un proceso COORDINADOR lee las cargas (PID llegada rafaga prioridad), arma los
// casos (carga x algoritmo) y los reparte a N procesos TRABAJADORES creados con
// fork(). La comunicacion es por pipes POSIX:
//   - un pipe de tareas por trabajador (coordinador -> trabajador i)
//   - un pipe de resultados compartido  (todos los trabajadores -> coordinador)
// El reparto es por demanda: cada vez que un trabajador responde, el coordinador
// le manda el siguiente caso pendiente.
//
// Protocolo de texto, un mensaje por linea (version 1):
//   coordinador -> trabajador
//     TAREA 1 <id> <carga> <algoritmo> <quantum> <n> <pid lleg raf prio> x n
//     FIN
//   trabajador -> coordinador
//     RESULTADO <id> <trab> <pid_so> <carga> <alg> <q> <nt> <ini fin pid> x nt
//               <nf> <pid lleg raf prio ini fin ret esp resp> x nf
//     ERROR <id> <trab> <pid_so> <detalle...>
//     ADIOS <trab> <pid_so> <casos_atendidos>
// Cada respuesta se escribe con un solo write() de tamano <= PIPE_BUF, asi es
// atomica aunque varios trabajadores escriban en el mismo pipe a la vez.
//
// Uso:
//   ./bin/practica
//   ./bin/practica --cargas entradas/carga1_cpu.txt entradas/carga2_interactiva.txt
//                  --quantums 2 4 --trabajadores 3 --retardo 50 --probar-error
//   (todo en una sola linea)
#include <algorithm>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "planificador.h"

using namespace std;

const int VERSION_PROTOCOLO = 1;
const int TIMEOUT_RESULTADO_MS = 10000;   // espera maxima por cada respuesta
const int TIMEOUT_CIERRE_MS = 5000;       // espera maxima para que terminen tras FIN

struct Caso {
    int id = -1;
    string carga;
    string algoritmo;
    int quantum = 0;
    vector<Proceso> procesos;
};

// ---------------------------------------------------------------------------
// Utilidades de tiempo y E/S
// ---------------------------------------------------------------------------
static double ahoraMs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void dormirMs(int ms) {
    timespec ts{ms / 1000, (ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

static bool escribirLinea(int fd, const string& s) {
    const char* p = s.data();
    size_t n = s.size();
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;   // p.ej. EPIPE si el otro extremo ya no existe
        }
        p += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

// Lee lineas completas de un descriptor, con timeout opcional
class LectorLineas {
    int fd;
    string buffer;

public:
    explicit LectorLineas(int f) : fd(f) {}
    // 1 = linea leida, 0 = EOF o error, -1 = timeout
    int leer(string& linea, int timeoutMs) {
        while (true) {
            size_t pos = buffer.find('\n');
            if (pos != string::npos) {
                linea = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);
                return 1;
            }
            if (timeoutMs >= 0) {
                pollfd p{fd, POLLIN, 0};
                int r = poll(&p, 1, timeoutMs);
                if (r == 0) return -1;
                if (r < 0) {
                    if (errno == EINTR) continue;
                    return 0;
                }
            }
            char tmp[4096];
            ssize_t n = read(fd, tmp, sizeof tmp);
            if (n < 0) {
                if (errno == EINTR) continue;
                return 0;
            }
            if (n == 0) return 0;
            buffer.append(tmp, static_cast<size_t>(n));
        }
    }
};

// ---------------------------------------------------------------------------
// Serializacion del protocolo
// ---------------------------------------------------------------------------
static string serializarTarea(const Caso& c) {
    ostringstream os;
    os << "TAREA " << VERSION_PROTOCOLO << " " << c.id << " " << c.carga << " " << c.algoritmo
       << " " << c.quantum << " " << c.procesos.size();
    for (const Proceso& p : c.procesos)
        os << " " << p.pid << " " << p.llegada << " " << p.rafaga << " " << p.prioridad;
    os << "\n";
    return os.str();
}

// Lanza runtime_error si el mensaje esta mal formado (c.id queda cargado si se pudo leer)
static void parsearTarea(const string& linea, Caso& c) {
    istringstream ss(linea);
    string tipo;
    int version = 0;
    size_t n = 0;
    ss >> tipo >> version >> c.id;
    if (tipo != "TAREA") throw runtime_error("tipo de mensaje no reconocido: " + tipo);
    if (version != VERSION_PROTOCOLO)
        throw runtime_error("version de protocolo no soportada: " + to_string(version));
    if (!(ss >> c.carga >> c.algoritmo >> c.quantum >> n)) throw runtime_error("TAREA incompleta");
    for (size_t i = 0; i < n; i++) {
        Proceso p;
        if (!(ss >> p.pid >> p.llegada >> p.rafaga >> p.prioridad))
            throw runtime_error("lista de procesos incompleta");
        p.idx = static_cast<int>(i);
        c.procesos.push_back(p);
    }
}

static string serializarResultado(int id, int trab, pid_t pidSo, const Resultado& r) {
    ostringstream os;
    os << "RESULTADO " << id << " " << trab << " " << pidSo << " " << r.carga << " "
       << r.algoritmo << " " << r.quantum << " " << r.linea.size();
    for (const Tramo& t : r.linea) os << " " << t.inicio << " " << t.fin << " " << t.pid;
    os << " " << r.filas.size();
    for (const Fila& f : r.filas)
        os << " " << f.pid << " " << f.llegada << " " << f.rafaga << " " << f.prioridad << " "
           << f.inicio << " " << f.fin << " " << f.retorno << " " << f.espera << " "
           << f.respuesta;
    os << "\n";
    return os.str();
}

static Resultado parsearResultado(istringstream& ss) {
    Resultado r;
    size_t nt = 0, nf = 0;
    ss >> r.carga >> r.algoritmo >> r.quantum >> nt;
    for (size_t i = 0; i < nt; i++) {
        Tramo t;
        ss >> t.inicio >> t.fin >> t.pid;
        r.linea.push_back(t);
    }
    ss >> nf;
    for (size_t i = 0; i < nf; i++) {
        Fila f;
        ss >> f.pid >> f.llegada >> f.rafaga >> f.prioridad >> f.inicio >> f.fin >> f.retorno >>
            f.espera >> f.respuesta;
        r.filas.push_back(f);
    }
    if (!ss) throw runtime_error("RESULTADO mal formado");
    completarMetricas(r);   // promedios se recalculan del lado del coordinador
    return r;
}

// ---------------------------------------------------------------------------
// Trabajador
// ---------------------------------------------------------------------------
static void trabajador(int numero, int fdTareas, int fdResultados, int retardoMs) {
    LectorLineas lector(fdTareas);
    string linea;
    int atendidos = 0;
    pid_t yo = getpid();

    while (lector.leer(linea, -1) == 1) {          // bloquea hasta recibir algo
        if (linea == "FIN") break;
        Caso c;
        string respuesta;
        try {
            parsearTarea(linea, c);
            Resultado r = ejecutar(c.algoritmo, c.procesos, c.quantum);
            r.carga = c.carga;
            dormirMs(retardoMs);                   // costo simulado del caso
            respuesta = serializarResultado(c.id, numero, yo, r);
            if (respuesta.size() > PIPE_BUF)      // tamano maximo del protocolo
                throw runtime_error("resultado supera PIPE_BUF (" + to_string(PIPE_BUF) + " bytes)");
        } catch (const exception& e) {             // el trabajador nunca muere en silencio
            respuesta = "ERROR " + to_string(c.id) + " " + to_string(numero) + " " +
                        to_string(yo) + " " + e.what() + "\n";
        }
        if (!escribirLinea(fdResultados, respuesta)) break;
        atendidos++;
    }
    escribirLinea(fdResultados, "ADIOS " + to_string(numero) + " " + to_string(yo) + " " +
                                    to_string(atendidos) + "\n");
    close(fdTareas);
    close(fdResultados);
    _exit(0);
}

// ---------------------------------------------------------------------------
// Coordinador
// ---------------------------------------------------------------------------
static string nombreCarga(const string& ruta) {
    return filesystem::path(ruta).stem().string();
}

static vector<Caso> armarCasos(const vector<string>& rutas, const vector<int>& quantums,
                               bool probarError) {
    vector<Caso> casos;
    for (const string& ruta : rutas) {
        vector<Proceso> procesos = leerProcesos(ruta);
        vector<pair<string, int>> algoritmos{{"FCFS", 0}, {"SJF", 0}, {"PRIORIDAD", 0}};
        for (int q : quantums) algoritmos.push_back({"RR", q});
        for (auto& [alg, q] : algoritmos) {
            Caso c;
            c.id = static_cast<int>(casos.size()) + 1;
            c.carga = nombreCarga(ruta);
            c.algoritmo = alg;
            c.quantum = q;
            c.procesos = procesos;
            casos.push_back(c);
        }
    }
    if (probarError && !casos.empty()) {   // caso invalido a proposito para probar ERROR
        Caso c = casos.front();
        c.id = static_cast<int>(casos.size()) + 1;
        c.carga = "prueba_error";
        c.algoritmo = "LOTERIA";
        c.quantum = 0;
        casos.push_back(c);
    }
    return casos;
}

struct Salida {
    map<int, Resultado> resultados;       // id_caso -> resultado
    map<int, string> errores;             // id_caso -> detalle
    vector<string> log;
};

static Salida coordinador(const vector<Caso>& casos, int nTrab, int retardoMs) {
    Salida s;
    double t0 = ahoraMs();
    auto registrar = [&](const string& texto) {
        cout << texto << "\n";
        s.log.push_back(texto);
    };
    auto ms = [&]() {
        ostringstream os;
        os << fixed << setprecision(1) << (ahoraMs() - t0) << " ms";
        return os.str();
    };

    signal(SIGPIPE, SIG_IGN);   // si un trabajador muere, write devuelve error en vez de matarnos

    int pRes[2];
    if (pipe(pRes) == -1) { perror("pipe resultados"); exit(1); }

    vector<int> fdTareas(nTrab, -1);
    vector<pid_t> pids(nTrab, -1);
    cout.flush();
    for (int i = 0; i < nTrab; i++) {
        int pT[2];
        if (pipe(pT) == -1) { perror("pipe tareas"); exit(1); }
        pid_t pid = fork();
        if (pid == -1) { perror("fork"); exit(1); }
        if (pid == 0) {
            // Hijo: cierra todo lo que no le corresponde
            close(pT[1]);
            close(pRes[0]);
            for (int j = 0; j < i; j++) close(fdTareas[j]);   // pipes de otros trabajadores
            trabajador(i + 1, pT[0], pRes[1], retardoMs);
        }
        close(pT[0]);
        fdTareas[i] = pT[1];
        pids[i] = pid;
    }
    close(pRes[1]);   // el coordinador solo lee resultados; asi detecta EOF si todos mueren

    registrar("[coordinador] " + to_string(casos.size()) + " casos para " + to_string(nTrab) +
              " trabajadores (reparto por demanda)");

    size_t siguiente = 0;
    auto enviar = [&](int trab) {   // trab es 1..nTrab
        if (siguiente >= casos.size()) return;
        const Caso& c = casos[siguiente++];
        string alg = c.algoritmo + (c.algoritmo == "RR" ? " q=" + to_string(c.quantum) : "");
        if (escribirLinea(fdTareas[trab - 1], serializarTarea(c)))
            registrar("[coordinador] -> TAREA " + to_string(c.id) + " (" + c.carga + " / " + alg +
                      ") a T" + to_string(trab) + "  [" + ms() + "]");
        else
            registrar("[coordinador] no se pudo enviar TAREA " + to_string(c.id));
    };
    for (int t = 1; t <= nTrab; t++) enviar(t);   // una tarea inicial a cada uno

    // Recolectar una respuesta por caso; cada respuesta libera al trabajador
    LectorLineas lector(pRes[0]);
    string linea;
    vector<string> adiosPendientes;
    size_t recibidos = 0;
    while (recibidos < casos.size()) {
        int r = lector.leer(linea, TIMEOUT_RESULTADO_MS);
        if (r == -1) { registrar("[coordinador] TIMEOUT esperando resultados"); break; }
        if (r == 0) { registrar("[coordinador] el pipe de resultados se cerro antes de tiempo"); break; }
        istringstream ss(linea);
        string tipo;
        int id = 0, trab = 0;
        long pidSo = 0;
        ss >> tipo;
        if (tipo == "RESULTADO") {
            ss >> id >> trab >> pidSo;
            try {
                s.resultados[id] = parsearResultado(ss);
                registrar("[coordinador] <- RESULTADO caso " + to_string(id) + " de T" +
                          to_string(trab) + "(pid " + to_string(pidSo) + ")  [" + ms() + "]");
            } catch (const exception& e) {
                s.errores[id] = e.what();
            }
            recibidos++;
            enviar(trab);
        } else if (tipo == "ERROR") {
            ss >> id >> trab >> pidSo;
            string detalle;
            getline(ss, detalle);
            if (!detalle.empty() && detalle[0] == ' ') detalle.erase(0, 1);
            s.errores[id] = detalle;
            registrar("[coordinador] <- ERROR caso " + to_string(id) + " de T" + to_string(trab) +
                      "(pid " + to_string(pidSo) + "): " + detalle);
            recibidos++;
            enviar(trab);
        } else if (tipo == "ADIOS") {
            adiosPendientes.push_back(linea);   // un trabajador que salio antes (no deberia)
        }
    }

    // ---------------- Cierre limpio ----------------
    for (int i = 0; i < nTrab; i++) {
        escribirLinea(fdTareas[i], "FIN\n");
        close(fdTareas[i]);                     // ademas del FIN, cerrar da EOF al trabajador
    }
    int adioses = 0;
    auto anotarAdios = [&](const string& l) {
        istringstream ss(l);
        string tipo;
        int trab, atendidos;
        long pidSo;
        ss >> tipo >> trab >> pidSo >> atendidos;
        adioses++;
        registrar("[coordinador] <- ADIOS de T" + to_string(trab) + "(pid " + to_string(pidSo) +
                  ") atendio " + to_string(atendidos) + " casos");
    };
    for (const string& l : adiosPendientes) anotarAdios(l);
    while (adioses < nTrab) {
        int r = lector.leer(linea, TIMEOUT_CIERRE_MS);
        if (r != 1) break;
        if (linea.rfind("ADIOS", 0) == 0) anotarAdios(linea);
    }
    close(pRes[0]);

    // Esperar a cada hijo con timeout; si no termina, se le mata
    string codigos;
    for (int i = 0; i < nTrab; i++) {
        int estado = 0;
        double limite = ahoraMs() + TIMEOUT_CIERRE_MS;
        pid_t w = 0;
        while ((w = waitpid(pids[i], &estado, WNOHANG)) == 0 && ahoraMs() < limite) dormirMs(10);
        if (w == 0) {
            registrar("[coordinador] T" + to_string(i + 1) + " no respondio; se termina con SIGKILL");
            kill(pids[i], SIGKILL);
            waitpid(pids[i], &estado, 0);
        }
        codigos += (i ? ", " : "") +
                   (WIFEXITED(estado) ? to_string(WEXITSTATUS(estado)) : string("senal"));
    }
    registrar("[coordinador] cierre limpio: " + to_string(adioses) + "/" + to_string(nTrab) +
              " ADIOS, codigos de salida [" + codigos + "]");
    return s;
}

// ---------------------------------------------------------------------------
// Reportes
// ---------------------------------------------------------------------------
static void guardarReportes(const vector<Caso>& casos, const Salida& s) {
    const string carpeta = "salidas/practica";
    filesystem::create_directories(carpeta);

    vector<string> ordenCargas;
    map<string, vector<Resultado>> porCarga;
    for (const auto& [id, r] : s.resultados) {
        if (!porCarga.count(r.carga)) ordenCargas.push_back(r.carga);
        porCarga[r.carga].push_back(r);
    }

    ostringstream res;
    res << fixed << setprecision(2);
    res << "COMPARACION DE ALGORITMOS POR CARGA\n\n";
    for (const string& carga : ordenCargas) {
        const vector<Resultado>& lista = porCarga[carga];
        ofstream det(carpeta + "/" + carga + "_detalle.txt");
        det << "CARGA: " << carga << "\n\n";
        for (const Resultado& r : lista) det << formatear(r) << "\n";

        res << "--- " << carga << " ---\n";
        res << "  " << left << setw(12) << "Algoritmo" << right << setw(9) << "Retorno" << setw(8)
            << "Espera" << setw(7) << "Resp" << setw(8) << "EspMax" << setw(9) << "Cambios" << "\n";
        for (const Resultado& r : lista) {
            res << "  " << left << setw(12) << nombreAlgoritmo(r) << right << setw(9) << r.promRetorno
                << setw(8) << r.promEspera << setw(7) << r.promRespuesta << setw(8) << r.maxEspera
                << setw(9) << r.cambiosContexto << "\n";
        }
        auto mejorEsp = min_element(lista.begin(), lista.end(), [](auto& a, auto& b) {
            return a.promEspera < b.promEspera;
        });
        auto mejorResp = min_element(lista.begin(), lista.end(), [](auto& a, auto& b) {
            return a.promRespuesta < b.promRespuesta;
        });
        res << "  Menor espera promedio: " << nombreAlgoritmo(*mejorEsp)
            << " | menor respuesta promedio: " << nombreAlgoritmo(*mejorResp) << "\n\n";
    }
    if (!s.errores.empty()) {
        res << "Casos con ERROR (manejados por el protocolo):\n";
        for (const auto& [id, detalle] : s.errores) res << "  caso " << id << ": " << detalle << "\n";
    }
    res << "\nCasos enviados: " << casos.size() << " | resultados: " << s.resultados.size()
        << " | errores: " << s.errores.size() << "\n";

    cout << "\n" << res.str();
    ofstream(carpeta + "/resumen_comparacion.txt") << res.str();
    ofstream log(carpeta + "/log_ipc.txt");
    for (const string& l : s.log) log << l << "\n";
    cout << "\nReportes en " << carpeta << "/\n";
}

// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    vector<string> cargas;
    vector<int> quantums;
    int nTrab = 3, retardoMs = 50;
    bool probarError = false;

    string modo;
    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "--cargas" || a == "--quantums") { modo = a; continue; }
        if (a == "--trabajadores" && i + 1 < argc) { nTrab = stoi(argv[++i]); modo.clear(); continue; }
        if (a == "--retardo" && i + 1 < argc) { retardoMs = stoi(argv[++i]); modo.clear(); continue; }
        if (a == "--probar-error") { probarError = true; modo.clear(); continue; }
        if (modo == "--cargas") cargas.push_back(a);
        else if (modo == "--quantums") quantums.push_back(stoi(a));
        else { cerr << "Argumento no reconocido: " << a << "\n"; return 2; }
    }
    if (cargas.empty()) cargas = {"entradas/carga1_cpu.txt", "entradas/carga2_interactiva.txt"};
    if (quantums.empty()) quantums = {2, 4};
    if (nTrab < 1) nTrab = 1;

    try {
        vector<Caso> casos = armarCasos(cargas, quantums, probarError);
        Salida s = coordinador(casos, nTrab, retardoMs);
        guardarReportes(casos, s);
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << "\n";
        return 2;
    }
    return 0;
}
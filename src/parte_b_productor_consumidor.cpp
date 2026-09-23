// parte_b_productor_consumidor.cpp
// Parte B del laboratorio (diapositiva 18): canal entre procesos con pipe (POSIX).
//
// El proceso padre crea dos pipes y dos hijos con fork():
//   PRODUCTOR  --(pipe trabajo)-->  CONSUMIDOR  --(pipe reporte)-->  PADRE
// Se envian 10 trabajos con id y duracion simulada. Por cada trabajo se guardan
// 4 marcas: envio, recepcion, inicio y fin (reloj CLOCK_MONOTONIC, comun a todos
// los procesos de la maquina).
//
// Se corren dos escenarios para separar el origen de la espera:
//   RAFAGA    : el productor manda los 10 trabajos seguidos.
//   ESPACIADO : el productor espera 400 ms entre envios (consumidor siempre libre).
//
// Protocolo (mensajes binarios de tamano fijo, cada write() es atomico en el pipe):
//   productor -> consumidor : TRABAJO {id, duracion_ms, t_envio} ... y al final FIN
//   consumidor -> padre     : REGISTRO {id, duracion, 4 marcas} ... y al final FIN_OK
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std;

enum TipoMensaje { TRABAJO = 1, FIN = 2, REGISTRO = 3, FIN_OK = 4 };

struct MsgTrabajo {
    int tipo;
    int id;
    int duracionMs;
    double tEnvio;
};

struct MsgRegistro {
    int tipo;
    int id;               // en FIN_OK lleva la cantidad de trabajos procesados
    int duracionMs;
    double tEnvio, tRecepcion, tInicio, tFin;
};

// Duraciones fijas (ms) para que la prueba sea reproducible
const int DURACIONES[10] = {200, 50, 100, 300, 50, 150, 100, 250, 50, 100};
const int SEPARACION_ESPACIADO_MS = 400;   // mayor que la duracion maxima

// Reloj monotonico del sistema en milisegundos
static double ahora() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void dormirMs(int ms) {
    timespec ts{ms / 1000, (ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

// Escribe todos los bytes (reintenta si write() escribe menos o lo interrumpe una senal)
static bool escribirTodo(int fd, const void* buf, size_t n) {
    const char* p = static_cast<const char*>(buf);
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

// Lee exactamente n bytes. Devuelve false si llega EOF (el otro extremo cerro) o error.
static bool leerTodo(int fd, void* buf, size_t n) {
    char* p = static_cast<char*>(buf);
    while (n > 0) {
        ssize_t r = read(fd, p, n);
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (r == 0) return false;
        p += r;
        n -= static_cast<size_t>(r);
    }
    return true;
}

static void morir(const char* que) {
    perror(que);
    exit(1);
}

// ---------------------------------------------------------------------------
static void productor(int fdTx, int separacionMs) {
    for (int i = 0; i < 10; i++) {
        MsgTrabajo m{TRABAJO, i + 1, DURACIONES[i], 0};
        m.tEnvio = ahora();
        if (!escribirTodo(fdTx, &m, sizeof m)) morir("productor write");
        if (separacionMs > 0) dormirMs(separacionMs);
    }
    MsgTrabajo fin{FIN, 0, 0, ahora()};
    escribirTodo(fdTx, &fin, sizeof fin);
    close(fdTx);
    _exit(0);
}

static void consumidor(int fdRx, int fdReporte) {
    int procesados = 0;
    MsgTrabajo m;
    while (leerTodo(fdRx, &m, sizeof m)) {        // se bloquea hasta que haya mensaje
        double tRecepcion = ahora();
        if (m.tipo == FIN) break;
        if (m.tipo != TRABAJO) continue;          // mensaje desconocido: se ignora
        double tInicio = ahora();
        dormirMs(m.duracionMs);                   // "trabajo" simulado
        double tFin = ahora();
        MsgRegistro reg{REGISTRO, m.id, m.duracionMs, m.tEnvio, tRecepcion, tInicio, tFin};
        if (!escribirTodo(fdReporte, &reg, sizeof reg)) morir("consumidor write");
        procesados++;
    }
    // Si el productor cerro sin FIN, leerTodo devuelve false y se sale igual (EOF)
    MsgRegistro finOk{FIN_OK, procesados, 0, 0, 0, 0, 0};
    escribirTodo(fdReporte, &finOk, sizeof finOk);
    close(fdRx);
    close(fdReporte);
    _exit(0);
}

// ---------------------------------------------------------------------------
static string correrEscenario(const string& nombre, int separacionMs) {
    int pTrabajo[2], pReporte[2];
    if (pipe(pTrabajo) == -1) morir("pipe trabajo");
    if (pipe(pReporte) == -1) morir("pipe reporte");

    double t0 = ahora();
    cout.flush();

    pid_t pidCons = fork();
    if (pidCons == -1) morir("fork consumidor");
    if (pidCons == 0) {
        close(pTrabajo[1]);    // el consumidor solo lee trabajos
        close(pReporte[0]);    // y solo escribe reportes
        consumidor(pTrabajo[0], pReporte[1]);
    }

    pid_t pidProd = fork();
    if (pidProd == -1) morir("fork productor");
    if (pidProd == 0) {
        close(pTrabajo[0]);    // el productor solo escribe trabajos
        close(pReporte[0]);
        close(pReporte[1]);
        productor(pTrabajo[1], separacionMs);
    }

    // El padre cierra lo que no usa. Si no cerrara pTrabajo[1], el consumidor
    // nunca veria EOF (pista de la diapositiva 16).
    close(pTrabajo[0]);
    close(pTrabajo[1]);
    close(pReporte[1]);

    vector<MsgRegistro> registros;
    int procesados = -1;
    MsgRegistro reg;
    while (leerTodo(pReporte[0], &reg, sizeof reg)) {
        if (reg.tipo == FIN_OK) {
            procesados = reg.id;
            break;
        }
        registros.push_back(reg);
    }
    close(pReporte[0]);

    int estProd = 0, estCons = 0;
    waitpid(pidProd, &estProd, 0);
    waitpid(pidCons, &estCons, 0);

    // ---------------- Tabla y CSV ----------------
    filesystem::create_directories("salidas");
    string minus = nombre;
    for (char& c : minus) c = static_cast<char>(tolower(c));
    string rutaCsv = "salidas/parte_b_" + minus + ".csv";
    ofstream csv(rutaCsv);
    csv << fixed << setprecision(3);
    csv << "id,duracion_ms,envio_ms,recepcion_ms,inicio_ms,fin_ms,canal_ms,arranque_ms,exceso_ms\n";

    ostringstream os;
    os << fixed;
    os << "=== Escenario " << nombre << " (separacion entre envios: " << separacionMs << " ms) ===\n";
    os << setw(3) << "ID" << setw(6) << "Dur" << setw(10) << "Envio" << setw(10) << "Recep"
       << setw(10) << "Inicio" << setw(10) << "Fin" << setw(10) << "Canal" << setw(8) << "Arranq"
       << setw(8) << "Exceso" << "   (todo en ms)\n";

    double sumCanal = 0, sumArr = 0, sumExc = 0, minCanal = 1e18, maxCanal = 0;
    for (const MsgRegistro& r : registros) {
        double envio = r.tEnvio - t0, recep = r.tRecepcion - t0;
        double ini = r.tInicio - t0, fin = r.tFin - t0;
        double canal = r.tRecepcion - r.tEnvio;                      // tiempo dentro del pipe
        double arranque = r.tInicio - r.tRecepcion;                  // recibir -> empezar
        double exceso = (r.tFin - r.tInicio) - r.duracionMs;         // retraso del SO al despertar
        sumCanal += canal; sumArr += arranque; sumExc += exceso;
        minCanal = min(minCanal, canal); maxCanal = max(maxCanal, canal);

        os << setw(3) << r.id << setw(6) << r.duracionMs << setprecision(2) << setw(10) << envio
           << setw(10) << recep << setw(10) << ini << setw(10) << fin << setprecision(3)
           << setw(10) << canal << setw(8) << arranque << setw(8) << exceso << "\n";
        csv << r.id << "," << r.duracionMs << "," << envio << "," << recep << "," << ini << ","
            << fin << "," << canal << "," << arranque << "," << exceso << "\n";
    }
    double n = registros.empty() ? 1 : registros.size();
    os << setprecision(3);
    os << "Promedios -> canal " << sumCanal / n << " ms | arranque " << sumArr / n
       << " ms | exceso SO " << sumExc / n << " ms\n";
    os << "Canal minimo " << minCanal << " ms | canal maximo " << maxCanal
       << " ms | trabajos procesados " << procesados << " | CSV: " << rutaCsv << "\n";
    os << "Hijos terminaron con codigo: productor " << WEXITSTATUS(estProd) << ", consumidor "
       << WEXITSTATUS(estCons) << "\n";
    return os.str();
}

int main() {
    string texto = correrEscenario("RAFAGA", 0) + "\n" +
                   correrEscenario("ESPACIADO", SEPARACION_ESPACIADO_MS);
    cout << texto;
    ofstream("salidas/parte_b_salida.txt") << texto;
    cout << "\nSalida guardada en salidas/parte_b_salida.txt\n";
    return 0;
}
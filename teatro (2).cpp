// ============================================================================
//  SISTEMA DE GESTION DEL TEATRO  (compatible con C++11)
//  Compilar:  g++ -std=c++11 -o teatro teatro.cpp
//  Menus:     flechas ARRIBA/ABAJO = mover | ENTER = elegir | ESC = volver
//
//  Datos en .txt (campos separados por '|'):
//      localidades.txt  eventos.txt  clientes.txt  tickets.txt
//
//  Carga masiva JSON (todas las claves son opcionales; "idTicket" tambien):
//  {
//    "localidades": [ { "seccion": "VIP", "asientosTotales": 50, "precioEntrada": 25.5 } ],
//    "eventos":     [ { "nombre": "Hamlet", "fecha": "25/12/2026",
//                       "horaInicio": "19:00", "horaFin": "21:30" } ],
//    "clientes":    [ { "cedula": "1234567890", "nombreCompleto": "Ana Perez",
//                       "telefono": "0991234567" } ],
//    "tickets":     [ { "idTicket": "T-00001", "eventoAsociado": "Hamlet",
//                       "seccionLocalidad": "VIP", "cedulaCliente": "1234567890" } ]
//  }
// ============================================================================

#ifdef _WIN32
#define NOMINMAX
#include <conio.h>
#include <windows.h>
#else
#include <csignal>
#include <dirent.h>
#include <termios.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace std;

// ============================================================================
// 1. ESTRUCTURA DE DATOS
//    Cada entidad (localidad, evento, cliente, ticket) es una tabla de filas de
//    texto. Asi el mismo codigo sirve para agregar, listar, modificar,
//    eliminar, guardar y cargar las cuatro.
// ============================================================================

typedef vector<string> Fila;
typedef string (*Validador)(string &valor, const Fila &fila); // "" = correcto

struct Campo
{
    const char *clave;    // nombre en el JSON
    const char *etiqueta; // nombre que ve el usuario
    const char *ayuda;    // formato esperado
    Validador validar;    // NULL = campo calculado (no se pide)
};

struct Entidad
{
    string titulo, archivo;
    vector<Campo> campos;
    vector<Fila> filas;
    int colTicket; // columna de "tickets" que apunta a esta entidad (-1 = ninguna)
};

Entidad localidades, eventos, clientes, tickets;

const long MAX_ASIENTOS = 100000;
const double MAX_PRECIO = 100000.0;

// ============================================================================
// 2. UTILIDADES
// ============================================================================

string str(long n)
{
    ostringstream o;
    o << n;
    return o.str();
}

string limpiar(const string &s)
{
    size_t i = 0, j = s.size();
    while (i < j && isspace((unsigned char)s[i]))
        i++;
    while (j > i && isspace((unsigned char)s[j - 1]))
        j--;
    return s.substr(i, j - i);
}

bool igual(string a, string b) // compara sin distinguir mayusculas
{
    for (size_t i = 0; i < a.size(); i++)
        a[i] = (char)tolower((unsigned char)a[i]);
    for (size_t i = 0; i < b.size(); i++)
        b[i] = (char)tolower((unsigned char)b[i]);
    return a == b;
}

bool soloDigitos(const string &s)
{
    if (s.empty())
        return false;
    for (size_t i = 0; i < s.size(); i++)
        if (!isdigit((unsigned char)s[i]))
            return false;
    return true;
}

bool aEntero(const string &s, long &n)
{
    if (!soloDigitos(s) || s.size() > 9)
        return false;
    n = strtol(s.c_str(), NULL, 10);
    return true;
}

bool aDecimal(const string &s, double &d)
{
    int puntos = 0;
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '.')
            puntos++;
        else if (!isdigit((unsigned char)s[i]))
            return false;
    }
    if (puntos > 1 || s.size() > 12 || s == ".")
        return false;
    d = strtod(s.c_str(), NULL);
    return true;
}

vector<string> dividir(const string &linea, char sep)
{
    vector<string> partes(1);
    for (size_t i = 0; i < linea.size(); i++)
    {
        if (linea[i] == sep)
            partes.push_back("");
        else
            partes.back() += linea[i];
    }
    return partes;
}

// Ancho en pantalla (un caracter con tilde ocupa 2 bytes en UTF-8)
size_t ancho(const string &s)
{
    size_t n = 0;
    for (size_t i = 0; i < s.size(); i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80)
            n++;
    return n;
}

string rellenar(const string &s, size_t w)
{
    return ancho(s) >= w ? s : s + string(w - ancho(s), ' ');
}

// ============================================================================
// 3. TERMINAL (Windows y Linux/macOS)
// ============================================================================

enum Tecla { ARRIBA, ABAJO, ENTER, ESCAPE, OTRA, FIN };

bool entradaCerrada = false; // true si se cerro el teclado (Ctrl+D / fin de archivo)

#ifndef _WIN32
termios terminalOriginal;
bool terminalGuardada = false;

void alInterrumpir(int) // Ctrl+C: dejar la terminal como estaba
{
    if (terminalGuardada)
        tcsetattr(STDIN_FILENO, TCSANOW, &terminalOriginal);
    _exit(130);
}
#endif

void prepararTerminal()
{
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#else
    if (tcgetattr(STDIN_FILENO, &terminalOriginal) == 0)
    {
        terminalGuardada = true;
        signal(SIGINT, alInterrumpir);
    }
#endif
}

void limpiarPantalla()
{
#ifdef _WIN32
    system("cls");
#else
    cout << "\033[2J\033[H" << flush;
#endif
}

void resaltar(bool activo) // marca la opcion seleccionada
{
    cout.flush();
#ifdef _WIN32
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), activo ? 0x70 : 0x07);
#else
    cout << (activo ? "\033[7m" : "\033[0m");
#endif
}

Tecla leerTecla()
{
    cout.flush();
    Tecla r = OTRA;
#ifdef _WIN32
    int c = _getch();
    if (c == 0 || c == 224)
    {
        int d = _getch();
        r = (d == 72) ? ARRIBA : (d == 80) ? ABAJO : OTRA;
    }
    else if (c == 13)
        r = ENTER;
    else if (c == 27)
        r = ESCAPE;
    else if (c == 4 || c == 26)
        r = FIN;
#else
    termios raw = terminalOriginal;
    if (terminalGuardada)
    {
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    unsigned char c = 0;
    if (read(STDIN_FILENO, &c, 1) <= 0 || c == 4)
        r = FIN;
    else if (c == '\n' || c == '\r')
        r = ENTER;
    else if (c == 27) // ESC solo, o ESC [ A/B (flechas)
    {
        r = ESCAPE;
        if (terminalGuardada)
        {
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 1;
            tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        }
        unsigned char b = 0, fin = 0;
        if (read(STDIN_FILENO, &b, 1) == 1 && (b == '[' || b == 'O'))
        {
            r = OTRA;
            while (read(STDIN_FILENO, &fin, 1) == 1 && !(fin >= 0x40 && fin <= 0x7E))
                ;
            r = (fin == 'A') ? ARRIBA : (fin == 'B') ? ABAJO : OTRA;
        }
    }
    if (terminalGuardada)
        tcsetattr(STDIN_FILENO, TCSANOW, &terminalOriginal);
#endif
    if (r == FIN)
        entradaCerrada = true;
    return r;
}

// ============================================================================
// 4. INTERFAZ: mensajes, menus con flechas, tablas
// ============================================================================

void pausar()
{
    cout << "\n  Presione una tecla para continuar..." << flush;
    leerTecla();
}

void mensaje(const string &tipo, const string &texto) // tipo: "OK", "ERROR", "AVISO"
{
    cout << "  [" << tipo << "] " << texto << "\n";
}

void cancelado()
{
    mensaje("AVISO", "Operacion cancelada. No se hicieron cambios.");
    pausar();
}

// Devuelve el indice elegido, o -1 con ESC.
int menuFlechas(const string &titulo, const vector<string> &opciones, const string &subtitulo = "", int sel = 0)
{
    const int VENTANA = 12;
    int n = (int)opciones.size(), inicio = 0;
    size_t w = 0;
    for (int i = 0; i < n; i++)
        w = max(w, ancho(opciones[i]));

    while (n > 0)
    {
        if (sel < inicio)
            inicio = sel;
        if (sel >= inicio + VENTANA)
            inicio = sel - VENTANA + 1;

        limpiarPantalla();
        cout << "=== " << titulo << " ===\n\n";
        if (!subtitulo.empty())
            cout << subtitulo << "\n\n";
        if (inicio > 0)
            cout << "     ^ mas arriba\n";
        for (int i = inicio; i < n && i < inicio + VENTANA; i++)
        {
            if (i == sel)
            {
                resaltar(true);
                cout << "  > " << rellenar(opciones[i], w) << "  ";
                resaltar(false);
                cout << "\n";
            }
            else
                cout << "    " << opciones[i] << "\n";
        }
        if (inicio + VENTANA < n)
            cout << "     v mas abajo\n";
        cout << "\n  [Flechas] mover   [ENTER] elegir   [ESC] volver\n";

        Tecla t = leerTecla();
        if (t == ARRIBA)
            sel = (sel - 1 + n) % n;
        else if (t == ABAJO)
            sel = (sel + 1) % n;
        else if (t == ENTER)
            return sel;
        else if (t == ESCAPE || t == FIN)
            return -1;
    }
    return -1;
}

bool confirmar(const string &pregunta) // la opcion inicial es "No", por seguridad
{
    vector<string> op;
    op.push_back("Si, continuar");
    op.push_back("No, cancelar");
    return menuFlechas("CONFIRMAR", op, pregunta, 1) == 0;
}

void imprimirTabla(const Entidad &e)
{
    vector<size_t> w;
    for (size_t c = 0; c < e.campos.size(); c++)
    {
        w.push_back(ancho(e.campos[c].etiqueta));
        for (size_t r = 0; r < e.filas.size(); r++)
            w[c] = max(w[c], ancho(e.filas[r][c]));
    }
    cout << "  ";
    for (size_t c = 0; c < w.size(); c++)
        cout << rellenar(e.campos[c].etiqueta, w[c]) << "  ";
    cout << "\n";
    for (size_t r = 0; r < e.filas.size(); r++)
    {
        cout << "  ";
        for (size_t c = 0; c < w.size(); c++)
            cout << rellenar(e.filas[r][c], w[c]) << "  ";
        cout << "\n";
    }
}

// Lee una linea. Devuelve false si se cerro la entrada (evita bucles infinitos).
bool leerLinea(string &s)
{
    if (!getline(cin, s))
    {
        cin.clear();
        entradaCerrada = true;
        return false;
    }
    s = limpiar(s);
    return true;
}

// ============================================================================
// 5. VALIDADORES (los mismos para teclado y JSON). Pueden normalizar el valor.
// ============================================================================

string vTexto(string &v, const Fila &)
{
    if (v.empty())
        return "No puede estar vacio.";
    if (v.size() > 60)
        return "Maximo 60 caracteres.";
    for (size_t i = 0; i < v.size(); i++)
    {
        unsigned char c = (unsigned char)v[i];
        if (c == '|')
            return "No se permite el caracter '|'.";
        if (c < 32 || c == 127)
            return "Contiene caracteres no permitidos.";
    }
    return "";
}

string vEntero(string &v, const Fila &)
{
    long n;
    if (!aEntero(v, n) || n < 1 || n > MAX_ASIENTOS)
        return "Ingrese un entero entre 1 y " + str(MAX_ASIENTOS) + ".";
    v = str(n);
    return "";
}

string vPrecio(string &v, const Fila &)
{
    replace(v.begin(), v.end(), ',', '.'); // acepta 12,50
    double d;
    if (!aDecimal(v, d) || d > MAX_PRECIO)
        return "Ingrese un precio entre 0 y 100000 (ejemplo: 15.50).";
    char buf[32];
    snprintf(buf, sizeof buf, "%.2f", d);
    v = buf;
    return "";
}

string vCedula(string &v, const Fila &)
{
    return (soloDigitos(v) && v.size() >= 6 && v.size() <= 13) ? "" : "Solo digitos, entre 6 y 13.";
}

string vTelefono(string &v, const Fila &)
{
    int digitos = 0;
    for (size_t i = 0; i < v.size(); i++)
    {
        if (isdigit((unsigned char)v[i]))
            digitos++;
        else if (!strchr("+- ()", v[i]))
            return "Solo digitos, espacios y los simbolos + - ( ).";
    }
    return (digitos >= 7 && digitos <= 15) ? "" : "Debe tener entre 7 y 15 digitos.";
}

string vFecha(string &v, const Fila &)
{
    const string formato = "Use el formato DD/MM/AAAA (ejemplo: 25/12/2026).";
    if (v.size() != 10 || v[2] != '/' || v[5] != '/')
        return formato;
    string d = v.substr(0, 2), m = v.substr(3, 2), a = v.substr(6, 4);
    long dia, mes, anio;
    if (!aEntero(d, dia) || !aEntero(m, mes) || !aEntero(a, anio))
        return formato;
    if (anio < 2000 || anio > 2100 || mes < 1 || mes > 12)
        return "Anio (2000-2100) o mes (01-12) invalido.";
    static const int dias[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool bisiesto = (anio % 4 == 0 && anio % 100 != 0) || anio % 400 == 0;
    if (dia < 1 || dia > dias[mes - 1] + ((mes == 2 && bisiesto) ? 1 : 0))
        return "Ese dia no existe en el mes indicado.";
    return "";
}

string vHora(string &v, const Fila &)
{
    const string formato = "Use el formato HH:MM en 24 horas (ejemplo: 19:30).";
    if (v.size() != 5 || v[2] != ':')
        return formato;
    long h, m;
    if (!aEntero(v.substr(0, 2), h) || !aEntero(v.substr(3, 2), m) || h > 23 || m > 59)
        return formato;
    return "";
}

string vHoraFin(string &v, const Fila &fila) // fila[2] = hora de inicio
{
    string e = vHora(v, fila);
    if (e.empty() && v <= fila[2])
        return "La hora de fin debe ser posterior a la de inicio.";
    return e;
}

// ============================================================================
// 6. BUSQUEDAS E INTEGRIDAD
// ============================================================================

int buscar(const Entidad &e, const string &clave) // la clave siempre es la columna 0
{
    for (size_t i = 0; i < e.filas.size(); i++)
        if (igual(e.filas[i][0], clave))
            return (int)i;
    return -1;
}

// Cuantos tickets apuntan a "valor" en la columna "col" de tickets
int contarTickets(int col, const string &valor)
{
    int n = 0;
    for (size_t i = 0; i < tickets.filas.size(); i++)
        if (igual(tickets.filas[i][col], valor))
            n++;
    return n;
}

// Regla: disponibles = totales - tickets vendidos. Se recalcula tras cada cambio.
void recalcular()
{
    for (size_t i = 0; i < localidades.filas.size(); i++)
    {
        Fila &f = localidades.filas[i];
        long total = 0;
        aEntero(f[1], total);
        long libres = total - contarTickets(2, f[0]);
        f[2] = str(libres < 0 ? 0 : libres);
    }
}

string siguienteId()
{
    for (long n = (long)tickets.filas.size() + 1;; n++)
    {
        char buf[32];
        snprintf(buf, sizeof buf, "T-%05ld", n);
        if (buscar(tickets, buf) == -1)
            return buf;
    }
}

// ============================================================================
// 7. GUARDAR Y CARGAR (.txt)
// ============================================================================

// Escribe en un archivo temporal y luego lo renombra, para no dejar archivos a medias.
bool guardarEntidad(const Entidad &e)
{
    string tmp = e.archivo + ".tmp";
    {
        ofstream f(tmp.c_str());
        if (!f)
            return false;
        for (size_t r = 0; r < e.filas.size(); r++)
        {
            for (size_t c = 0; c < e.filas[r].size(); c++)
                f << (c ? "|" : "") << e.filas[r][c];
            f << "\n";
        }
        f.flush();
        if (!f)
            return false;
    }
#ifdef _WIN32
    std::remove(e.archivo.c_str()); // en Windows rename no sobrescribe
#endif
    return std::rename(tmp.c_str(), e.archivo.c_str()) == 0;
}

bool guardarTodo()
{
    bool ok = true;
    ok = guardarEntidad(localidades) && ok;
    ok = guardarEntidad(eventos) && ok;
    ok = guardarEntidad(clientes) && ok;
    ok = guardarEntidad(tickets) && ok;
    return ok;
}

void guardarYAvisar()
{
    if (!guardarTodo())
    {
        mensaje("ERROR", "No se pudieron guardar los datos en disco (revise permisos o espacio).");
        pausar();
    }
}

void cargarEntidad(Entidad &e, vector<string> &avisos)
{
    ifstream f(e.archivo.c_str());
    if (!f)
        return; // primera ejecucion: el archivo aun no existe
    string linea;
    int danadas = 0;
    while (getline(f, linea))
    {
        if (limpiar(linea).empty())
            continue;
        Fila p = dividir(limpiar(linea), '|');
        if (p.size() == e.campos.size())
            e.filas.push_back(p);
        else
            danadas++;
    }
    if (danadas > 0)
        avisos.push_back(e.archivo + ": se ignoraron " + str(danadas) + " linea(s) danadas.");
}

void cargarTodo()
{
    vector<string> avisos;
    cargarEntidad(localidades, avisos);
    cargarEntidad(eventos, avisos);
    cargarEntidad(clientes, avisos);
    cargarEntidad(tickets, avisos);
    recalcular();
    if (!avisos.empty())
    {
        limpiarPantalla();
        for (size_t i = 0; i < avisos.size(); i++)
            mensaje("AVISO", avisos[i]);
        pausar();
    }
}

void iniciarEntidades()
{
    Campo l[] = {{"seccion", "Seccion", "nombre de la seccion", vTexto},
                 {"asientosTotales", "Asientos totales", "entero", vEntero},
                 {"asientosDisponibles", "Disponibles", "", NULL},
                 {"precioEntrada", "Precio", "ejemplo: 15.50", vPrecio}};
    Campo e[] = {{"nombre", "Nombre de la obra", "", vTexto},
                 {"fecha", "Fecha", "DD/MM/AAAA", vFecha},
                 {"horaInicio", "Hora inicio", "HH:MM", vHora},
                 {"horaFin", "Hora fin", "HH:MM", vHoraFin}};
    Campo c[] = {{"cedula", "Cedula", "solo digitos", vCedula},
                 {"nombreCompleto", "Nombre completo", "", vTexto},
                 {"telefono", "Telefono", "", vTelefono}};
    Campo t[] = {{"idTicket", "ID", "", vTexto},
                 {"eventoAsociado", "Evento", "", vTexto},
                 {"seccionLocalidad", "Localidad", "", vTexto},
                 {"cedulaCliente", "Cedula", "", vTexto}};

    localidades.titulo = "LOCALIDADES";
    localidades.archivo = "localidades.txt";
    localidades.campos.assign(l, l + 4);
    localidades.colTicket = 2;
    eventos.titulo = "EVENTOS";
    eventos.archivo = "eventos.txt";
    eventos.campos.assign(e, e + 4);
    eventos.colTicket = 1;
    clientes.titulo = "CLIENTES";
    clientes.archivo = "clientes.txt";
    clientes.campos.assign(c, c + 3);
    clientes.colTicket = 3;
    tickets.titulo = "TICKETS";
    tickets.archivo = "tickets.txt";
    tickets.campos.assign(t, t + 4);
    tickets.colTicket = -1;
}

// ============================================================================
// 8. OPERACIONES GENERICAS: agregar, listar, modificar, eliminar
// ============================================================================

// Elige un registro con las flechas. Devuelve su posicion o -1.
int elegir(const Entidad &e, const string &accion)
{
    if (e.filas.empty())
    {
        limpiarPantalla();
        mensaje("AVISO", "No hay registros en " + e.titulo + ".");
        pausar();
        return -1;
    }
    string cabecera = "  ";
    for (size_t c = 0; c < e.campos.size(); c++)
        cabecera += string(c ? " | " : "") + e.campos[c].etiqueta;

    vector<string> etiquetas;
    for (size_t r = 0; r < e.filas.size(); r++)
    {
        string s;
        for (size_t c = 0; c < e.filas[r].size(); c++)
            s += (c ? " | " : "") + e.filas[r][c];
        etiquetas.push_back(s);
    }
    return menuFlechas(accion + " - " + e.titulo, etiquetas, cabecera);
}

// Pide cada campo con validacion. Al modificar, ENTER en blanco conserva el valor;
// al agregar, ENTER en blanco cancela. "pos" = posicion que se edita (-1 si es nuevo).
bool pedirCampos(const Entidad &e, Fila &f, int pos, bool modificando)
{
    cout << "  " << (modificando ? "(ENTER en blanco conserva el valor actual)" : "(ENTER en blanco cancela)") << "\n\n";
    for (size_t i = 0; i < e.campos.size(); i++)
    {
        const Campo &c = e.campos[i];
        if (!c.validar)
            continue;
        while (true)
        {
            cout << "  " << c.etiqueta;
            if (c.ayuda[0])
                cout << " (" << c.ayuda << ")";
            if (modificando)
                cout << " [" << f[i] << "]";
            cout << ": " << flush;

            string v;
            if (!leerLinea(v))
                return false;
            if (v.empty())
            {
                if (modificando)
                    break;
                return false;
            }
            string err = c.validar(v, f);
            int repetido = (i == 0 && err.empty()) ? buscar(e, v) : -1;
            if (repetido != -1 && repetido != pos)
                err = "Ya existe un registro con ese valor.";
            if (!err.empty())
            {
                mensaje("ERROR", err);
                continue;
            }
            f[i] = v;
            break;
        }
    }
    return true;
}

void agregar(Entidad &e)
{
    limpiarPantalla();
    cout << "=== NUEVO REGISTRO: " << e.titulo << " ===\n\n";
    Fila f(e.campos.size());
    if (!pedirCampos(e, f, -1, false))
        return cancelado();
    e.filas.push_back(f);
    recalcular();
    guardarYAvisar();
    mensaje("OK", "Registro agregado.");
    pausar();
}

void listar(const Entidad &e)
{
    limpiarPantalla();
    cout << "=== LISTA: " << e.titulo << " ===\n\n";
    if (e.filas.empty())
        mensaje("AVISO", "No hay registros.");
    else
        imprimirTabla(e);
    pausar();
}

void modificar(Entidad &e)
{
    int pos = elegir(e, "MODIFICAR");
    if (pos < 0)
        return;
    Fila nueva = e.filas[pos];
    string vieja = nueva[0];

    limpiarPantalla();
    cout << "=== MODIFICAR: " << vieja << " ===\n\n";
    if (!pedirCampos(e, nueva, pos, true))
        return cancelado();

    // La capacidad de una localidad no puede quedar por debajo de lo ya vendido
    long total = 0;
    if (&e == &localidades && aEntero(nueva[1], total) && total < contarTickets(2, vieja))
    {
        mensaje("ERROR", "Ya hay " + str(contarTickets(2, vieja)) + " tickets vendidos: el aforo no puede ser menor.");
        pausar();
        return;
    }
    if (!confirmar("Guardar los cambios en '" + nueva[0] + "'?"))
        return cancelado();

    e.filas[pos] = nueva;
    if (e.colTicket >= 0 && !igual(vieja, nueva[0])) // los tickets siguen apuntando al registro
        for (size_t i = 0; i < tickets.filas.size(); i++)
            if (igual(tickets.filas[i][e.colTicket], vieja))
                tickets.filas[i][e.colTicket] = nueva[0];
    recalcular();
    guardarYAvisar();
    limpiarPantalla();
    mensaje("OK", "Registro actualizado.");
    pausar();
}

void eliminar(Entidad &e)
{
    int pos = elegir(e, "ELIMINAR");
    if (pos < 0)
        return;
    string clave = e.filas[pos][0];
    int afectados = (e.colTicket >= 0) ? contarTickets(e.colTicket, clave) : 0;

    string pregunta = "Eliminar '" + clave + "'?";
    if (afectados > 0)
        pregunta += "\n\n  ATENCION: tiene " + str(afectados) + " ticket(s). Tambien se ANULARAN\n  y sus asientos quedaran libres.";
    if (!confirmar(pregunta))
        return cancelado();

    if (afectados > 0)
    {
        vector<Fila> restantes;
        for (size_t i = 0; i < tickets.filas.size(); i++)
            if (!igual(tickets.filas[i][e.colTicket], clave))
                restantes.push_back(tickets.filas[i]);
        tickets.filas = restantes;
    }
    e.filas.erase(e.filas.begin() + pos);
    recalcular();
    guardarYAvisar();
    limpiarPantalla();
    mensaje("OK", "Eliminado." + (afectados > 0 ? " Tickets anulados: " + str(afectados) + "." : string()));
    pausar();
}

// ============================================================================
// 9. VENTA DE TICKETS
// ============================================================================

void venderTicket()
{
    if (localidades.filas.empty() || eventos.filas.empty() || clientes.filas.empty())
    {
        limpiarPantalla();
        mensaje("AVISO", "Antes de vender, registre en este orden:");
        cout << "    1. Localidades " << (localidades.filas.empty() ? "(falta)" : "(listo)") << "\n"
             << "    2. Eventos     " << (eventos.filas.empty() ? "(falta)" : "(listo)") << "\n"
             << "    3. Clientes    " << (clientes.filas.empty() ? "(falta)" : "(listo)") << "\n";
        pausar();
        return;
    }

    int il = elegir(localidades, "VENTA 1/3");
    if (il < 0)
        return;
    if (localidades.filas[il][2] == "0")
    {
        mensaje("ERROR", "La localidad esta agotada. Elija otra.");
        pausar();
        return;
    }
    int ie = elegir(eventos, "VENTA 2/3");
    if (ie < 0)
        return;
    int ic = elegir(clientes, "VENTA 3/3");
    if (ic < 0)
        return;

    const Fila &l = localidades.filas[il], &ev = eventos.filas[ie], &cl = clientes.filas[ic];
    if (!confirmar("Confirmar la venta?\n\n  Evento:    " + ev[0] + " (" + ev[1] + ")\n  Localidad: " + l[0] +
                   "\n  Cliente:   " + cl[1] + "\n  Total:     $" + l[3]))
        return cancelado();

    Fila t(4);
    t[0] = siguienteId();
    t[1] = ev[0];
    t[2] = l[0];
    t[3] = cl[0];
    tickets.filas.push_back(t);
    recalcular();
    guardarYAvisar();
    limpiarPantalla();
    mensaje("OK", "Ticket vendido. ID: " + t[0]);
    pausar();
}

// ============================================================================
// 10. CARGA MASIVA JSON
//     Lector minimo: solo admite la forma { "clave": [ { "campo": valor }, ... ] }
// ============================================================================

typedef vector<pair<string, string> > Objeto; // campo -> valor (como texto)
typedef vector<pair<string, vector<Objeto> > > Documento;

class LectorJson
{
public:
    LectorJson(const string &t) : s(t), p(0)
    {
        if (s.compare(0, 3, "\xEF\xBB\xBF") == 0) // marca BOM de UTF-8
            p = 3;
    }

    Documento leer()
    {
        Documento doc;
        esperar('{');
        if (!siguiente('}'))
        {
            do
            {
                string clave = texto();
                esperar(':');
                doc.push_back(make_pair(clave, lista()));
            } while (siguiente(','));
            esperar('}');
        }
        ws();
        if (p != s.size())
            error("hay contenido despues del final del JSON");
        return doc;
    }

private:
    const string &s;
    size_t p;

    void error(const string &m)
    {
        int linea = 1;
        for (size_t i = 0; i < p && i < s.size(); i++)
            if (s[i] == '\n')
                linea++;
        throw runtime_error("linea " + str(linea) + ": " + m);
    }
    void ws()
    {
        while (p < s.size() && isspace((unsigned char)s[p]))
            p++;
    }
    void esperar(char c)
    {
        ws();
        if (p >= s.size() || s[p] != c)
            error(string("se esperaba '") + c + "'");
        p++;
    }
    bool siguiente(char c)
    {
        ws();
        if (p < s.size() && s[p] == c)
        {
            p++;
            return true;
        }
        return false;
    }

    vector<Objeto> lista()
    {
        vector<Objeto> l;
        esperar('[');
        if (!siguiente(']'))
        {
            do
            {
                Objeto o;
                esperar('{');
                if (!siguiente('}'))
                {
                    do
                    {
                        string k = texto();
                        esperar(':');
                        o.push_back(make_pair(k, valor()));
                    } while (siguiente(','));
                    esperar('}');
                }
                l.push_back(o);
            } while (siguiente(','));
            esperar(']');
        }
        return l;
    }

    string valor() // un texto entre comillas, o un numero/true/false/null
    {
        ws();
        if (p < s.size() && s[p] == '"')
            return texto();
        size_t i = p;
        while (p < s.size() && (isalnum((unsigned char)s[p]) || s[p] == '.' || s[p] == '-' || s[p] == '+'))
            p++;
        if (p == i)
            error("valor no valido (no se admiten objetos ni listas anidadas)");
        return s.substr(i, p - i);
    }

    string texto()
    {
        esperar('"');
        string o;
        while (true)
        {
            if (p >= s.size())
                error("texto sin cerrar");
            unsigned char c = (unsigned char)s[p++];
            if (c == '"')
                return o;
            if (c < 0x20)
                error("salto de linea dentro de un texto");
            if (c != '\\')
            {
                o += (char)c;
                continue;
            }
            if (p >= s.size())
                error("texto sin cerrar");
            char e = s[p++];
            if (e == 'n' || e == 't' || e == 'r')
                o += ' ';
            else if (e == '"' || e == '\\' || e == '/')
                o += e;
            else if (e == 'u') // caracter Unicode \uXXXX (se guarda como UTF-8)
            {
                if (p + 4 > s.size())
                    error("secuencia \\u incompleta");
                unsigned long cp = strtoul(s.substr(p, 4).c_str(), NULL, 16);
                p += 4;
                if (cp < 0x80)
                    o += (char)cp;
                else if (cp < 0x800)
                {
                    o += (char)(0xC0 | (cp >> 6));
                    o += (char)(0x80 | (cp & 0x3F));
                }
                else
                {
                    o += (char)(0xE0 | (cp >> 12));
                    o += (char)(0x80 | ((cp >> 6) & 0x3F));
                    o += (char)(0x80 | (cp & 0x3F));
                }
            }
            else
                error(string("secuencia de escape invalida '\\") + e + "'");
        }
    }
};

// Valida un registro del JSON con las mismas reglas del teclado y lo agrega.
// Devuelve "" si se agrego, o el motivo del rechazo.
string importarRegistro(Entidad &e, const Objeto &obj)
{
    bool esTicket = (&e == &tickets);
    Fila f(e.campos.size());

    for (size_t i = 0; i < e.campos.size(); i++)
    {
        const Campo &c = e.campos[i];
        if (!c.validar)
            continue; // campo calculado
        const string *v = NULL;
        for (size_t k = 0; k < obj.size(); k++)
            if (obj[k].first == c.clave)
                v = &obj[k].second;
        if (!v)
        {
            if (esTicket && i == 0)
                continue; // idTicket es opcional
            return string("falta el campo '") + c.clave + "'";
        }
        f[i] = limpiar(*v);
        string err = c.validar(f[i], f);
        if (!err.empty())
            return string(c.clave) + ": " + err;
    }

    if (esTicket) // todo lo que referencia debe existir y debe haber asientos
    {
        int ie = buscar(eventos, f[1]), il = buscar(localidades, f[2]), ic = buscar(clientes, f[3]);
        if (ie < 0)
            return "el evento '" + f[1] + "' no existe";
        if (il < 0)
            return "la localidad '" + f[2] + "' no existe";
        if (ic < 0)
            return "el cliente '" + f[3] + "' no existe";
        long total = 0;
        aEntero(localidades.filas[il][1], total);
        if (contarTickets(2, f[2]) >= total)
            return "la localidad '" + f[2] + "' no tiene asientos disponibles";
        f[1] = eventos.filas[ie][0];
        f[2] = localidades.filas[il][0];
        if (f[0].empty())
            f[0] = siguienteId();
    }
    if (buscar(e, f[0]) != -1)
        return "ya existe '" + f[0] + "'";
    e.filas.push_back(f);
    return "";
}

// Busca los archivos ".json" de la carpeta actual (no entra a subcarpetas).
vector<string> listarArchivosJson()
{
    vector<string> nombres;
#ifdef _WIN32
    WIN32_FIND_DATAA d;
    HANDLE h = FindFirstFileA("*.json", &d);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                nombres.push_back(d.cFileName);
        } while (FindNextFileA(h, &d));
        FindClose(h);
    }
#else
    DIR *dir = opendir(".");
    if (dir)
    {
        struct dirent *e;
        while ((e = readdir(dir)) != NULL)
        {
            string n = e->d_name;
            if (n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0)
                nombres.push_back(n);
        }
        closedir(dir);
    }
#endif
    sort(nombres.begin(), nombres.end());
    return nombres;
}

// Deja elegir un .json con flechas si hay alguno en la carpeta; si no,
// o si el usuario prefiere escribir la ruta, la pide por teclado.
// Devuelve "" si el usuario cancela (ESC).
string elegirArchivoJson()
{
    vector<string> archivos = listarArchivosJson();
    if (!archivos.empty())
    {
        vector<string> op = archivos;
        op.push_back("(Escribir otra ruta a mano)");
        int r = menuFlechas("CARGA MASIVA DESDE JSON", op,
                            "Se encontraron estos archivos .json en esta carpeta:");
        if (r < 0)
            return "";
        if (r < (int)archivos.size())
            return archivos[r];
        // sigue abajo para pedir la ruta a mano
    }

    limpiarPantalla();
    cout << "=== CARGA MASIVA DESDE JSON ===\n\n"
         << "  No se encontraron archivos .json en esta carpeta (o eligio escribirla).\n"
         << "  Ruta del archivo [datos.json]: " << flush;
    string ruta;
    if (!leerLinea(ruta))
        return "";
    if (ruta.size() >= 2 && (ruta[0] == '"' || ruta[0] == '\'') && ruta[ruta.size() - 1] == ruta[0])
        ruta = ruta.substr(1, ruta.size() - 2); // quita comillas de rutas pegadas
    return ruta.empty() ? "datos.json" : ruta;
}

void cargaMasiva()
{
    string ruta = elegirArchivoJson();
    if (ruta.empty())
        return; // el usuario cancelo con ESC

    limpiarPantalla();
    cout << "=== CARGA MASIVA DESDE JSON ===\n\n"
         << "  Los registros se AGREGAN a los datos actuales y se guardan en los .txt.\n"
         << "  Los repetidos o invalidos se omiten y se informan.\n\n"
         << "  Archivo seleccionado: " << ruta << "\n\n";

    ifstream archivo(ruta.c_str(), ios::binary);
    if (!archivo)
    {
        mensaje("ERROR", "No se pudo abrir '" + ruta + "' (no existe o no hay permiso).");
        return pausar();
    }
    stringstream buffer;
    buffer << archivo.rdbuf();
    string contenido = buffer.str();
    if (contenido.empty() || contenido.size() > 20 * 1024 * 1024)
    {
        mensaje("ERROR", "El archivo esta vacio o supera los 20 MB.");
        return pausar();
    }

    Documento doc;
    try // primero se lee TODO; si hay un error de sintaxis no se toca ningun dato
    {
        LectorJson lector(contenido);
        doc = lector.leer();
    }
    catch (const exception &ex)
    {
        mensaje("ERROR", string("JSON invalido (") + ex.what() + ").");
        mensaje("AVISO", "No se modifico ningun dato.");
        return pausar();
    }
    if (!confirmar("Archivo leido correctamente.\n\n  Cargar '" + ruta + "'?"))
        return cancelado();

    // Se importa en el orden de dependencias: localidades, eventos, clientes, tickets
    Entidad *ents[4] = {&localidades, &eventos, &clientes, &tickets};
    const char *claves[4] = {"localidades", "eventos", "clientes", "tickets"};
    int leidos[4] = {0, 0, 0, 0}, agregados[4] = {0, 0, 0, 0};
    vector<string> problemas;

    for (int k = 0; k < 4; k++)
        for (size_t d = 0; d < doc.size(); d++)
            if (doc[d].first == claves[k])
                for (size_t r = 0; r < doc[d].second.size(); r++)
                {
                    leidos[k]++;
                    string err = importarRegistro(*ents[k], doc[d].second[r]);
                    if (err.empty())
                        agregados[k]++;
                    else
                        problemas.push_back(string(claves[k]) + " #" + str((long)r + 1) + ": " + err);
                }

    recalcular();
    bool guardado = guardarTodo();

    limpiarPantalla();
    cout << "=== RESULTADO DE LA CARGA ===\n\n";
    for (int k = 0; k < 4; k++)
        cout << "  " << rellenar(claves[k], 12) << " leidos: " << leidos[k] << "   agregados: " << agregados[k]
             << "   rechazados: " << leidos[k] - agregados[k] << "\n";
    cout << "\n";
    if (guardado)
        mensaje("OK", "Datos guardados en los archivos .txt.");
    else
        mensaje("ERROR", "Los datos NO se pudieron escribir en disco.");
    for (size_t i = 0; i < problemas.size() && i < 10; i++)
        cout << "    - " << problemas[i] << "\n";
    if (problemas.size() > 10)
        cout << "    ... y " << problemas.size() - 10 << " mas.\n";
    pausar();
}

// ============================================================================
// 11. MENUS Y MAIN
// ============================================================================

void submenu(Entidad &e, const string &ayuda)
{
    vector<string> op;
    op.push_back("Agregar");
    op.push_back("Listar");
    op.push_back("Modificar");
    op.push_back("Eliminar");
    op.push_back("Volver al menu principal");
    int sel = 0;
    while (true)
    {
        int r = menuFlechas(e.titulo, op, ayuda, sel);
        if (r < 0 || r == 4)
            return;
        sel = r;
        if (r == 0)
            agregar(e);
        else if (r == 1)
            listar(e);
        else if (r == 2)
            modificar(e);
        else
            eliminar(e);
    }
}

void submenuTickets()
{
    vector<string> op;
    op.push_back("Vender ticket");
    op.push_back("Listar tickets");
    op.push_back("Anular ticket");
    op.push_back("Volver al menu principal");
    int sel = 0;
    while (true)
    {
        int r = menuFlechas("TICKETS", op, "Venta y control de entradas.", sel);
        if (r < 0 || r == 3)
            return;
        sel = r;
        if (r == 0)
            venderTicket();
        else if (r == 1)
            listar(tickets);
        else
            eliminar(tickets);
    }
}

void menuPrincipal()
{
    vector<string> op;
    op.push_back("1. Localidades   - secciones, aforo y precios");
    op.push_back("2. Eventos       - obras, fechas y horarios");
    op.push_back("3. Clientes      - registro de compradores");
    op.push_back("4. Tickets       - venta y anulacion");
    op.push_back("5. Carga masiva  - importar datos desde JSON");
    op.push_back("6. Salir");
    int sel = 0;
    while (!entradaCerrada)
    {
        string resumen = "Localidades: " + str((long)localidades.filas.size()) + "  Eventos: " + str((long)eventos.filas.size()) +
                         "  Clientes: " + str((long)clientes.filas.size()) + "  Tickets: " + str((long)tickets.filas.size());
        int r = menuFlechas("SISTEMA DE GESTION DEL TEATRO", op, resumen, sel);
        if (r < 0 || r == 5)
        {
            if (entradaCerrada || confirmar("Desea salir del sistema?"))
                return;
            continue;
        }
        sel = r;
        if (r == 0)
            submenu(localidades, "Secciones del teatro, su aforo y el precio de la entrada.");
        else if (r == 1)
            submenu(eventos, "Obras de teatro con su fecha y horario.");
        else if (r == 2)
            submenu(clientes, "Personas que compran entradas.");
        else if (r == 3)
            submenuTickets();
        else
            cargaMasiva();
    }
}

int main()
{
    prepararTerminal();
    iniciarEntidades();
    cargarTodo();
    try
    {
        menuPrincipal();
    }
    catch (const exception &ex)
    {
        cout << "\n";
        mensaje("ERROR", string("Error inesperado: ") + ex.what());
    }
    if (!guardarTodo())
        mensaje("ERROR", "No se pudieron guardar los datos al salir.");
    limpiarPantalla();
    cout << "Datos guardados. Hasta pronto.\n";
    return 0;
}

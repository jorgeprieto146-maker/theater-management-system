# theater-management-system

# 🎭 Sistema de Gestión de Teatro (Theater Management System)

Un sistema de gestión para salas de teatro desarrollado en **C++** que permite administrar obras, asientos, reservas y ventas de boletos de manera eficiente. Los datos del sistema se persisten y estructuran utilizando archivos en formato **JSON**.

---

## 🚀 Características principales

- **Gestión de Funciones y Obras:** Registro, modificación y consulta de horarios, salas y obras disponibles.
- **Control de Asientos y Boletos:** Asignación, reserva y liberación de asientos en tiempo real.
- **Persistencia de Datos en JSON:** Almacenamiento local de la configuración del teatro, inventario y registros de boletos mediante archivos `.json`.
- **Lógica Eficiente en C++:** Uso de contenedores estándar y algoritmos optimizados para el manejo rápido en memoria.

---

## 🛠️ Tecnologías utilizadas

- **Lenguaje:** C++ (C++17 o superior recomendado)
- **Formato de datos:** JSON
- **Librerías recomendadas:** 
  - [`nlohmann/json`](https://github.com/nlohmann/json) para el parseo y manipulación de datos JSON en C++.
  - Librería estándar de C++ (`<fstream>`, `<vector>`, `<map>`, `<memory>`).

---

## 📁 Estructura del proyecto

```text
theater-management-system/
├── include/              # Archivos de cabecera (.h / .hpp)
├── src/                  # Código fuente (.cpp)
├── data/                 # Archivos JSON de persistencia (obras.json, reservas.json)
├── build/                # Archivos de compilación (ignorado en git)
├── README.md             # Documentación del proyecto
└── .gitignore            # Filtro para binarios y temporales

# Búsqueda de claves AES

Proyecto del curso **CC3169 — Computación Paralela y Distribuida**, Universidad
del Valle de Guatemala. Integrantes: Adriana Palacios, Bryan Martínez y Daniela
Ramírez.

El código está separado según la forma de buscar la clave. Las partes que usan
varias versiones se reúnen en una misma carpeta para evitar repetirlas.

## Organización

| Carpeta o archivo | Qué contiene |
| --- | --- |
| `src/sequential/` | Programa que prueba las claves una por una con un solo trabajador. |
| `src/distributed/` | Versiones que reparten la búsqueda entre varios trabajadores mediante MPI. |
| `src/common/` | Funciones que comparten las versiones, como cifrar un mensaje o revisar una posible clave. |
| `tests/` | Pruebas para comprobar que los programas funcionan como esperamos. |
| `scripts/` | Programas de apoyo para repetir pruebas y guardar sus resultados. |
| `guides/` | Guías compartidas para organizar el código y medir las búsquedas. |
| `docs/` | Planes, propuestas y diagramas locales que se mantienen fuera de Git. |
| `results/` | Resultados de las pruebas y comparaciones. |
| `build/` | Archivos que se crean al preparar los programas para ejecutarlos. |
| `Makefile` | Instrucciones para preparar los programas y ejecutar las pruebas. |

## Requisitos

- Un compilador de C compatible con C11, como Clang o GCC, y `make`.
- OpenSSL 3, para cifrar y descifrar los mensajes.
- Open MPI, que proporciona `mpicc` para compilar y `mpirun` para ejecutar las versiones distribuidas.
- `pkg-config`, para localizar las bibliotecas durante la compilación.
- Python 3, para ejecutar las pruebas y comparar los tiempos. Los scripts no necesitan paquetes adicionales.

En macOS, si todavía no tienes las herramientas de compilación, instálalas con:

```sh
xcode-select --install
```

Con Homebrew instalado, puedes instalar las demás herramientas con:

```sh
brew install open-mpi openssl@3 pkgconf python
```

`pkgconf` proporciona el comando `pkg-config`. Las páginas de
[Open MPI](https://formulae.brew.sh/formula/open-mpi),
[OpenSSL 3](https://formulae.brew.sh/formula/openssl@3) y
[pkgconf](https://formulae.brew.sh/formula/pkgconf) describen estos paquetes.

## Compilación

Ejecuta los comandos desde la carpeta principal del proyecto:

```sh
make -j4
```

Los tres programas se crean en `build/bin/`:

| Programa | Qué ejecuta |
| --- | --- |
| `aes_search_file` | Búsqueda secuencial. |
| `aes_search_mpi_blocks` | Búsqueda distribuida por bloques. |
| `aes_search_mpi_hybrid` | Búsqueda distribuida con el recorrido híbrido. |

También puedes compilar únicamente la versión secuencial con `make sequential`
o las distribuidas con `make mpi`. Para borrar una compilación anterior y volver
a preparar los programas:

```sh
make clean
make -j4
```

El Makefile busca OpenSSL mediante `pkg-config` o Homebrew. Si tienes una
instalación en otra carpeta, puedes indicar su ubicación mediante
`OPENSSL_PREFIX` al ejecutar `make`.

## Archivo de entrada

El programa recibe un archivo de texto que debe contener la frase
"es una prueba de" y ocupar entre 1 y 4096 bytes. Puedes crear el archivo del
ejemplo con:

```sh
printf '%s\n' "Esta es una prueba de proyecto 2" > mensaje.txt
```

## Ejecución

La versión secuencial prueba las claves una por una, comenzando en cero:

```sh
./build/bin/aes_search_file mensaje.txt 123456 56
```

La versión distribuida base reparte la búsqueda en cuatro bloques, uno por
proceso, y recorre cada bloque en orden:

```sh
mpirun -np 4 ./build/bin/aes_search_mpi_blocks mensaje.txt 123456 56
```

La versión híbrida conserva ese reparto y combina ocho claves nuevas en orden
con una del recorrido mezclado:

```sh
mpirun -np 4 ./build/bin/aes_search_mpi_hybrid mensaje.txt 123456 56 8 1
```

## Pruebas de funcionamiento

Para ejecutar todas las comprobaciones del proyecto:

```sh
make check
```

También puedes ejecutarlas por separado:

```sh
make test
make test-mpi-blocks
make test-mpi-hybrid
```


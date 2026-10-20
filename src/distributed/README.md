# Búsqueda entre varios trabajadores

Estos programas usan MPI para repartir la búsqueda. MPI permite que varias
copias de un programa trabajen al mismo tiempo y se envíen avisos. Aquí llamamos
trabajador a cada copia, y trabajador 0 al que prepara el mensaje y reúne los
resultados.

| Programa | Cómo busca |
| --- | --- |
| `aes_search_mpi` | Reparte las claves por turnos. Por ejemplo, con dos trabajadores, uno prueba 0, 2, 4… y el otro 1, 3, 5… Comprueba el inicio del mensaje. |
| `aes_search_mpi_blocks` | Da a cada trabajador un grupo de claves consecutivas. Cada uno recorre su grupo en orden y busca la frase `es una prueba de` en el mensaje recuperado. |
| `aes_search_mpi_hybrid` | Conserva esos grupos, pero combina pruebas consecutivas con los «tiros revueltos» de la propuesta. Busca la misma frase. |

Las versiones por bloques e híbrida comparten la lectura del archivo, el cifrado
y la comprobación de las claves. Así podemos comparar sus formas de buscar con
el mismo tipo de ejercicio.

La [versión secuencial de archivos](../sequential/README.md) comparte esas mismas
operaciones y sirve como referencia para calcular speedup y eficiencia.

Cada programa tiene su archivo `.c` con el mismo nombre. Los pasos compartidos
están en estas otras partes:

| Archivo | Para qué sirve |
| --- | --- |
| `file_search_mpi.c` y `.h` | Preparan el mensaje y lo comparten con los trabajadores antes de empezar. |
| `hybrid_search_mpi.c` y `.h` | Realizan la búsqueda híbrida y coordinan los avisos para terminar. |
| `../common/hybrid_order.c` y `.h` | Eligen la próxima posición que se probará sin repetir claves. |

Los archivos `.h` explican cómo usar las funciones y los `.c` contienen sus pasos.

## La variante híbrida

La propuesta usa cuatro trabajadores. Cada uno recibe una parte distinta del
conjunto de claves. Dentro de su parte, prueba `k` claves consecutivas y luego
una elegida en un orden diferente. Repite esos pasos hasta encontrar una
coincidencia o terminar todas sus claves.

Los tiros revueltos siguen un orden calculado que contiene todas las claves del
grupo una sola vez. A ese reordenamiento se le llama **permutación**. Una semilla,
que es un número de entrada, permite repetir el mismo orden en otra ejecución.

También hay que evitar que un tiro revuelto repita una clave ya probada por el
recorrido consecutivo. El programa puede calcular en qué lugar aparece cada
clave en el otro recorrido y saltarla si ya pasó por allí. Por ejemplo, si las
claves son 0, 1, 2, 3 y 4, un recorrido posible con `k=2` es 0, 1, 3, 2, 4: al
llegar de nuevo a 3 se salta porque ya se probó.

Esto permite recordar el avance con unos pocos números, sin guardar una lista
de todas las claves visitadas. El cálculo también admite grupos de distintos
tamaños. Su adaptación a esos tamaños sigue el método descrito por
[Black y Rogaway, sección 4](https://www.cs.ucdavis.edu/~rogaway/papers/subset.pdf).
El orden calculado puede ayudar a llegar antes a una clave lejana, pero no
asegura que todas las búsquedas sean más rápidas.

## Cómo se avisan para terminar

El trabajador 0 lee el archivo, cifra el mensaje y comparte los datos necesarios
para probar las claves. La respuesta correcta solo se usa al preparar ese
mensaje.

Durante la búsqueda, cada trabajador revisa si recibió un aviso de parada tras
una tanda de hasta 256 cálculos del recorrido. El límite cuenta también el
trabajo de descartar claves repetidas y de recalcular una posición que quedó
fuera del grupo. Si se agota la tanda, guarda el cálculo pendiente para revisar
los avisos y continuar desde allí. La pausa conserva el orden de las claves y
no se cuenta como una prueba de clave.

Cuando encuentra una coincidencia o termina su grupo, se lo informa al trabajador
0. Si ya no tiene claves, espera el resultado de los demás.

El trabajador 0 avisa a todos cuando hay una coincidencia o cuando todos
terminaron sin encontrarla. Enviar el aviso no obliga a esperar en ese instante;
antes de cerrar, el programa comprueba que todos los avisos hayan terminado de
enviarse y recibirse. Las funciones de MPI que permiten hacerlo se describen en
[su documentación](https://www.mpi-forum.org/docs/mpi-5.0/mpi50-report/node76.htm).

Al final se suman las claves probadas y se toma el tiempo del trabajador que más
tardó. Ese tiempo incluye la búsqueda y sus avisos, pero deja fuera la lectura
del archivo, la preparación del mensaje y la impresión del resultado.

## Cómo probarla

Desde la carpeta principal del proyecto:

```sh
make -j4
mpirun -np 4 ./build/bin/aes_search_mpi_hybrid mensaje.txt 12345 16 8 1
```

El número 4 indica cuántos trabajadores participan. Después del nombre del
programa se escriben estos datos:

| Dato del ejemplo | Qué significa |
| --- | --- |
| `mensaje.txt` | Archivo con el texto que se va a cifrar. Debe contener `es una prueba de` y ocupar entre 1 y 4096 bytes. |
| `12345` | Clave que se usa para preparar el ejercicio y que después se intenta encontrar. |
| `16` | Tamaño de la búsqueda: con 16 bits se consideran 65,536 posibles claves. |
| `8` | Valor de `k`: ocho pruebas consecutivas antes de un tiro revuelto. |
| `1` | Semilla que elige el orden de los tiros revueltos. |

Se pueden omitir los últimos tres datos. Sus valores iniciales son 56 bits,
`k=16` y semilla 1. La clave debe estar entre cero y la cantidad de claves menos
uno. La propuesta completa considera 56 bits; el ejemplo usa 16 para probarla
en menos tiempo. El programa también acepta otros números de trabajadores.

Para comprobar su funcionamiento y comparar `k=8`, `16` y `32`:

```sh
make test
make test-mpi-blocks
make test-mpi-hybrid
python3 scripts/compare_distributed.py --bits 20 --key 1048575 --processes 4 --repetitions 5 --k 8 16 32 --seed 1
```

La comparación hace primero una ejecución de preparación y luego repite cada
variante, incluida la referencia secuencial. Guarda los tiempos, las claves
probadas, los datos de la máquina y las salidas completas en `results/runs/`.
`measurements.csv` contiene cada medición y `summary.csv` reúne las medianas,
el speedup y la eficiencia. Son tablas que se pueden abrir en una hoja de cálculo.

Cada repetición usa el mismo mensaje, clave y cantidad de trabajadores, y la
misma semilla para la variante híbrida. El dato de inicio del cifrado se genera
de nuevo en cada ejecución. Conviene probar varias claves y semillas antes de
elegir `k`, porque una búsqueda corta por sí sola no permite decidir cuál
funcionará mejor en general.

La [guía de medición](../../guides/benchmark_methodology.md) explica cómo hacer
la comparación y cómo omitir la referencia secuencial cuando tardaría demasiado.

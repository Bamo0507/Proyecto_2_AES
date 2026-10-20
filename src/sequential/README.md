# Búsqueda con un solo trabajador

El programa prueba una clave tras otra, sin repartir el trabajo con MPI.

| Programa | Para qué sirve |
| --- | --- |
| `aes_search_file` | Referencia del proyecto actual: lee un archivo y busca la frase `es una prueba de` en el mensaje recuperado. |

`aes_search_file` comparte la lectura, el cifrado, la conversión de claves y la
comprobación de la frase con las versiones MPI por bloques e híbrida. Permite
comparar cuánto tarda el mismo tipo de búsqueda con uno o varios trabajadores.

Desde la carpeta principal del proyecto:

```sh
make sequential
./build/bin/aes_search_file mensaje.txt 123456 56
```

Después del nombre del programa se indican el archivo, la clave usada para
preparar el ejercicio y la cantidad de bits que se van a buscar. Si se omite el
último dato, se usan 56 bits. El archivo debe contener la frase y ocupar entre
1 y 4096 bytes.

La búsqueda comienza en cero. Las claves grandes del enunciado pueden tardar
muchísimo en esta versión, aunque se encuentren pronto al repartirlas entre
cuatro trabajadores. Conviene empezar con casos pequeños para comprobar el
funcionamiento.

La [guía de medición](../../guides/benchmark_methodology.md) explica qué tiempo
se cuenta y cómo calcular speedup y eficiencia con el comparador.

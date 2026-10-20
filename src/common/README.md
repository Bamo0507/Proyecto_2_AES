# Partes que comparten las versiones

Esta carpeta reúne las funciones que se usan en más de una versión. Por ejemplo,
las versiones CTR necesitan construir posibles claves y comprobar si pueden
descifrar un mensaje. Mantener esas funciones juntas permite corregirlas en un
solo lugar. El ejemplo original ECB se conserva aparte y contiene sus propias
funciones.

Llamamos **candidata** a una posible clave que vamos a probar. En las versiones
distribuidas, cada trabajador es una copia del programa que busca en una parte
del conjunto de claves.

| Archivo | Para qué sirve |
| --- | --- |
| `search_options` | Revisa los datos que se escriben al iniciar `aes_search_ctr` y `aes_search_mpi`, como la cantidad de claves y el mensaje. |
| `aes_ctr` | Cifra y descifra mensajes con ayuda de OpenSSL, una biblioteca de cifrado. También construye las claves que se van a probar. |
| `search_reporting` | Muestra la posible clave encontrada, el mensaje recuperado y el tiempo de búsqueda de `aes_search_ctr` y `aes_search_mpi`. |
| `file_search` | Lee el archivo de entrada y comprueba si una clave recupera la frase `es una prueba de`. Lo comparten la secuencial de archivos y las versiones MPI por bloques e híbrida. |
| `hybrid_order` | Decide qué clave probar después en la versión híbrida, alternando claves consecutivas con otras tomadas en un orden diferente. Evita repetir pruebas. |
| `search_timing` | Consulta el mismo reloj para medir las búsquedas secuenciales que comparten módulos. |

Cada nombre corresponde a dos archivos. El `.h` explica qué ofrece esa parte y
cómo usarla. El `.c` contiene los pasos que realizan el trabajo.

## Cómo se relacionan

Primero se revisan los datos de entrada y se prepara un mensaje cifrado con una
clave conocida. Esa clave sirve para crear el ejercicio; durante la búsqueda se
prueban posibles claves sin consultar la respuesta.

Después, cada versión decide en qué orden hará las pruebas. Para cada candidata
usa las funciones de cifrado de esta carpeta. Al terminar, reúne lo encontrado y
muestra el resultado.

Hay dos formas de comprobar una candidata. `aes_search_ctr` y `aes_search_mpi` comparan
hasta los primeros ocho bytes del mensaje, es decir, una pequeña parte de su
inicio. Si esa parte es muy corta, más de una clave puede producir una
coincidencia. La secuencial de archivos y las versiones MPI por bloques e híbrida
descifran el mensaje completo y buscan la frase `es una prueba de` en cualquier
posición.

En la versión híbrida, `k` indica cuántas claves consecutivas se prueban antes de
hacer un intento en otro orden. Por ejemplo, con `k=8` se hacen ocho pruebas en
orden y luego una de las llamadas «tiros revueltos» en la propuesta. Un número
llamado semilla permite volver a generar el mismo orden. Se omiten las claves
que ya se habían probado por cualquiera de los dos caminos.

El recorrido puede pausar un cálculo para que se revisen los avisos de parada.
Conserva su avance y, al retomarlo, entrega las claves en el mismo orden.

Los tamaños del mensaje se miden en bytes, la unidad usada para contar su espacio
en memoria. Las versiones que comparan el inicio aceptan hasta 256 bytes y las que leen un
archivo aceptan hasta 4096. Los comentarios de cada función indican cuánto
espacio se necesita y qué significa su respuesta.

## Cómo leer los nombres del código

Los nombres se mantienen en inglés para seguir el mismo criterio en todos los
archivos. Estos aparecen en varias funciones:

| Nombre | Qué representa |
| --- | --- |
| `search_options` | Los datos elegidos al iniciar el programa, como el mensaje y cuántas claves se van a probar. |
| `aes_context` | Los datos de trabajo que OpenSSL reutiliza para cifrar y descifrar. |
| `search_problem` | El mensaje cifrado y los datos necesarios para intentar recuperarlo. |
| `search_result` | La clave encontrada, cuántas se probaron y cuánto tardó la búsqueda. |
| `candidate` | El número de la posible clave que se está probando. |

En el reparto por grupos, `first_candidate` es la primera clave y
`candidate_limit` la primera que queda fuera. Por ejemplo, un grupo que empieza
en 100 y termina antes de 104 contiene 100, 101, 102 y 103. Dentro del grupo,
`candidate_position` cuenta desde cero; la posición 3 corresponde a la clave
103. `sequential_trials_per_turn` es el valor de `k`.

La [guía de las versiones distribuidas](../distributed/README.md) explica cómo
se reparte el trabajo y cómo probar los valores de `k`.

Este es un proyecto de Computación Gráfica hecho en C++ con OpenGL. Carga un modelo 3D de un caballero montado en un caballo y lo mueve por una curva de Bézier. 
El modelo también da vueltas solo, y con la tecla F se puede parar. Los puntos de la curva se pueden mover con el teclado y la cámara sigue al modelo.

Usé OpenGL, GLFW, GLAD, GLM, tiny_gltf y stb_image.

Controles: con W A S D se gira el modelo, con Q y E se acerca o se aleja, con F se para el giro, con Espacio se pausa el recorrido, con los números del 1 al 4 se elige un punto de la curva y con las flechas se mueve. 
Con V se muestra u oculta la curva, con C la cámara sigue o no al modelo y con ESC se sale.

Para ejecutarlo hay que cambiar las rutas del modelo y del fondo en el código, porque ahora están con la ruta de mi computador.

Por Wilmer Salamanca 

Arquitectura del código

El proyecto tiene dos archivos: Shader.h y Proyecto_Prueba.cpp.

Shader.h

Tiene la clase Shader, que compila y enlaza un programa de shaders de OpenGL y permite enviarle valores (setInt, setBool, setVec3, setVec4, setMat4). Debajo de la clase están los códigos fuente de los shaders, guardados como texto en funciones:

Modelo (modelVertexSrc, modelFragmentSrc): dibuja el caballero con su textura y una luz direccional fija.
Fondo (backgroundVertexSrc, backgroundFragmentSrc): dibuja la imagen de fondo en toda la pantalla con un solo triángulo.
Línea (lineVertexSrc, lineFragmentSrc): dibuja la curva y los puntos de control con un color liso.
Proyecto_Prueba.cpp

Está dividido en cuatro partes:

1. Datos globales. Aquí está la estructura Mesh, que guarda lo necesario para dibujar cada pedazo del modelo (VAO, cantidad de índices, textura, matriz de mundo y color). También están la lista meshes, la caché de texturas y los límites del modelo (bmin y bmax).

2. Funciones auxiliares.

bezier() calcula un punto de la curva cúbica a partir de los 4 puntos de control y un valor t entre 0 y 1.
drawDynamic() dibuja líneas o puntos con un buffer que se actualiza en cada fotograma.
teclaPulsada() detecta una sola vez cada pulsación, y se usa para las teclas que activan o desactivan algo.

3. Carga del modelo. Las funciones se llaman en cadena:

loadModel → processNode → loadPrimitive → getTexture

loadModel abre el .glb con tiny_gltf.
processNode recorre los nodos de la escena y acumula sus transformaciones.
loadPrimitive lee posiciones, normales, coordenadas UV e índices, y crea los buffers de OpenGL.
getTexture carga la textura con stb_image y la guarda en la caché para no repetirla.
loadBackground carga la imagen de fondo.

4. main(). Se divide en dos momentos.

Preparación (una sola vez): crea la ventana y el contexto de OpenGL, carga el modelo y el fondo, compila los tres shaders, prepara el buffer dinámico, calcula el centro y la escala del modelo, y define los puntos de la curva con las variables de estado.

Bucle principal (cada fotograma):

Entrada: lee el teclado (girar, zoom, F, Espacio, V, C, selección y movimiento de puntos).
Actualización: avanza t sobre la curva y aumenta el ángulo de giro si F está activo.
Dibujo del fondo: sin prueba de profundidad, para que quede detrás de todo.
Dibujo del modelo: calcula la posición con bezier(), arma la matriz de transformación, actualiza la cámara para que siga al modelo y dibuja cada Mesh.
Dibujo de la curva: encima de todo, si V está activo.
Intercambio de buffers y lectura de eventos.
Orden de las transformaciones del modelo
trasladar a la posición de la curva
   → rotar (pitch, yaw + giro automático)
   → escalar
   → mover el centro del modelo al origen

Se lee de abajo hacia arriba: primero se centra el modelo, luego se escala, después se rota sobre su propio centro y al final se coloca en el punto de la curva.







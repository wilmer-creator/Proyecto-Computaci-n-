Este es un proyecto de Computación Gráfica hecho en C++ con OpenGL. Carga un modelo 3D de un caballero montado en un caballo y lo mueve por una curva de Bézier. 
El modelo también da vueltas solo, y con la tecla F se puede parar. Los puntos de la curva se pueden mover con el teclado y la cámara sigue al modelo.

Usé OpenGL, GLFW, GLAD, GLM, tiny_gltf y stb_image.

Controles: con W A S D se gira el modelo, con Q y E se acerca o se aleja, con F se para el giro, con Espacio se pausa el recorrido, con los números del 1 al 4 se elige un punto de la curva y con las flechas se mueve. 
Con V se muestra u oculta la curva, con C la cámara sigue o no al modelo y con ESC se sale.

Para ejecutarlo hay que cambiar las rutas del modelo y del fondo en el código, porque ahora están con la ruta de mi computador.

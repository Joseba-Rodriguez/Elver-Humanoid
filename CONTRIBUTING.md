# Contribuir a Elver 🤖

¡Gracias por querer echar una mano (o un servo)!

## Ideas y fallos
Abre un [issue](https://github.com/Joseba-Rodriguez/Elver-Humanoid/issues/new/choose): hay plantillas para fallos, ideas y para enseñar tu propio Elver.

## Pull requests
1. Haz un fork y crea una rama: `git checkout -b mi-mejora`.
2. Si tocas `humanoide-automatico.ino`, pasa el simulador antes de enviar:
   ```bash
   cd simulador/mega-pc
   python compilar.py && python puente.py --prueba && python puente.py --limites 60
   ```
3. **Regla de oro: los límites de los servos no se tocan a la ligera.** Un ángulo de más rompe piezas impresas. Todo movimiento nuevo debe usar las poses y constantes que ya existen, o justificar los nuevos valores.
4. Mantén el estilo: código y comentarios en español, nombres claros.

## Comandos nuevos
Si añades un comando de un carácter, actualiza también:
- `procesarComando()` y la ayuda (`?`) del sketch,
- `COMANDOS` en `simulador/mega-pc/puente.py`,
- la tabla de comandos del README.

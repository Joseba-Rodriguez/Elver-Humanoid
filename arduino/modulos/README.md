# 🧩 Módulos sueltos

Programas para probar y calibrar **cada parte por separado** antes de montar el robot completo. Para el robot entero, usa [`../humanoide-automatico`](../humanoide-automatico).

| Sketch | Para qué sirve |
|---|---|
| [`cuello-automatico`](cuello-automatico) | Movimientos lentos y cortos de la cabeza (3 servos, pines 36-38) |
| [`ojos-automatic`](ojos-automatic) | Parpadeo y mirada automáticos (6 servos, pines 39-44) |
| [`ojos-calibracion`](ojos-calibracion) | Mueve un servo de los ojos al ángulo que escribas, para encontrar sus límites reales |
| [`mano-automatica`](mano-automatica) | Poses automáticas de la mano, la muñeca y el codo, con límites de seguridad (pines 30-35 y 45) |
| [`mano-flex`](mano-flex) | La mano copia tus dedos con un guante de sensores flex (A0-A5) |

Cada carpeta tiene el mismo nombre que su `.ino`, como pide el Arduino IDE.

1. **ENUNCIADO DEL PROYECTO**

Se requiere diseñar e implementar un sistema IoT para un edificio residencial de mínimo tres departamentos, que permita administrar de manera automatizada el acceso principal, la seguridad de la puerta de ingreso, la puerta de la cochera y la iluminación de cada departamento.

El sistema deberá estar basado principalmente en un microcontrolador ESP32 y deberá contar con un lector RFID instalado en la puerta principal para identificar a los usuarios autorizados.

Cuando un usuario acerque una tarjeta o llave RFID autorizada al lector, el sistema deberá validar su identificación, mostrar en la pantalla LCD un mensaje indicando que el acceso ha sido permitido y proceder a desbloquear la puerta principal.

La puerta principal deberá disponer de un sensor magnético que permita conocer si esta se encuentra abierta o cerrada. Si el sensor detecta que la puerta ha sido abierta sin que previamente se haya producido una identificación RFID válida, el sistema deberá considerar el evento como una apertura no autorizada y activar un buzzer como alarma.

La puerta de la cochera común deberá contar con un mecanismo de apertura y cierre accionado mediante un servomotor, el cual será controlado por el ESP32.

Cada uno de los tres departamentos deberá contar con un sistema de iluminación independiente que pueda ser encendido o apagado remotamente mediante una interfaz IoT conectada al ESP32.

En la entrada principal se instalará además una cámara, cuya finalidad será realizar la vigilancia de la zona de acceso y permitir la captura o transmisión de imágenes mediante Wi-Fi.

El sistema deberá mantener las funciones básicas de seguridad y control local incluso cuando no exista conexión a Internet.

1. **ALCANCE**

El proyecto comprenderá el desarrollo de un prototipo o maqueta de un edificio residencial compuesto por:

Tres departamentos como mínimo.

Una cochera común.

Una puerta principal común.

Un sistema de control de acceso RFID.

Un sistema de detección de apertura de la puerta.

Un sistema de alarma.

Una pantalla LCD.

Una cámara.

Tres circuitos independientes de iluminación.

Un sistema de control remoto mediante Wi-Fi.

El proyecto estará orientado principalmente a fines académicos y de demostración.

1. **LISTA DE COMPONENTES Y MATERIALES**

N.º Componente Cantidad Función

1 ESP32 DevKit 1 Control principal

2 ESP32-CAM 1 Sistema de vigilancia

3 Lector RFID RC522 1 Identificación

4 Tarjetas/llaveros RFID 3 o más Identificación de usuarios

5 LCD 16x2 I2C 1 Información al usuario

6 Sensor Magnétio 1 Detección de puerta

7 Buzzer 1 Alarma

8 Servomotor 1 Control de cochera

9 LEDs 15 o más Iluminación

10 Resistencias Según diseño Protección de LEDs

11 Protoboard 1 o más Prototipo

12 Cables Dupont Según necesidad Conexiones

13 Fuente de alimentación Según diseño Alimentación

14 Router Wi-Fi 1 Comunicación

15 Maqueta del edificio 1 Representación física

/**
 * ============================================================================
 * PROYECTO: Sistema IoT Residencial Inteligente - Edificio Multifamiliar
 * MICROCONTROLADOR: ESP32 DevKit V1 (30 pines)
 * ARCHIVO: firmware.ino
 *
 * ESPECIFICACIÓN Y REQUERIMIENTOS ACTUALIZADOS:
 *  1. Control y Seguridad con Máquina de Estados Finitos (FSM) no bloqueante:
 *     - ESTADO_CERRADO: Reposo, LCD "Edificio Seguro"/"Acerque tarjeta".
 *     - ESTADO_ACCESO_PERMITIDO: LCD "Acceso Permitido"/"Bienv. [Dpto]",
 *       tono aprobatorio en buzzer pasivo, abre automáticamente la puerta con
 *       el SERVOMOTOR (90°) por 3 segundos y luego se cierra solo a 0°.
 *     - ESTADO_PUERTA_ABIERTA_OK: LCD "Puerta Abierta"/"Pase adelante".
 *     - ESTADO_ALARMA: LCD "!ALERTA SEGURID!"/"Puerta Forzada", sirena oscilante
 *       en buzzer pasivo. Actualización en tiempo real en la Web vía AJAX.
 *     - Acceso denegado: "Acceso Denegado" por 2s + tono grave de rechazo.
 *
 *  2. Motor Paso a Paso Cochera (Driver ULN2003: IN1, IN2, IN3, IN4):
 *     - Sentido Antihorario (CCW): ABRE la cochera (3 segundos de duración).
 *     - Sentido Horario (CW): CIERRA la cochera (3 segundos de duración).
 *     - Ejecución 100% no bloqueante (micros/millis) sin frenar el bucle principal.
 *     - Desenergiza las 4 bobinas al detenerse para evitar sobrecalentamiento.
 *
 *  3. Servomotor en la Puerta Principal (GPIO 25):
 *     - Apertura automática a 90° durante 3 segundos al autenticar RFID y cierre a 0°.
 *
 *  4. Buzzer Pasivo con Tonos Frecuenciales Diferenciados (GPIO 13):
 *     - Tono Aprobatorio: Melodía ascendente agradable (1760 Hz -> 2637 Hz).
 *     - Tono Desaprobatorio: Tono grave de error (400 Hz -> pausa -> 300 Hz).
 *     - Tono de Alarma: Sirena modulada oscilante (850 Hz <-> 1900 Hz).
 *
 *  5. Servidor Web HTTP local (puerto 80) y Dashboard en VIVO:
 *     - Sondeo en tiempo real cada 1 segundo (/api/status) vía JavaScript/AJAX.
 *     - Banner de alerta de intrusión dinámico y botón para silenciar la alarma.
 *     - Botones para abrir (antihorario 3s) y cerrar (horario 3s) la cochera.
 *     - Botones independientes para las luces de los departamentos 1, 2 y 3.
 *     - Soporte dual para cámaras: Webcam USB (Raspberry Pi) y ESP32-CAM.
 *
 * MAPA DE PINES:
 *  - LCD I2C 16x2: SDA = GPIO 21, SCL = GPIO 22 (Dirección 0x27)
 *  - Lector RC522 SPI: SS = GPIO 5, SCK = GPIO 18, MOSI = GPIO 23, MISO = GPIO 19, RST = GPIO 4
 *  - Sensor Magnético Puerta: GPIO 27 (INPUT_PULLUP, LOW = Cerrada, HIGH = Abierta)
 *  - Buzzer Pasivo: GPIO 13 (Salida PWM por tonos no bloqueantes)
 *  - Servomotor Puerta: GPIO 25 (0° = Cerrado, 90° = Abierto)
 *  - Motor Paso a Paso Cochera (Driver ULN2003):
 *      IN1 = GPIO 14
 *      IN2 = GPIO 12
 *      IN3 = GPIO 16
 *      IN4 = GPIO 17
 *  - Relés Iluminación (Active-LOW: LOW = ON, HIGH = OFF):
 *      Luz Dpto 1 = GPIO 26
 *      Luz Dpto 2 = GPIO 32
 *      Luz Dpto 3 = GPIO 33
 * ============================================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WebServer.h>

// ============================================================================
// 1. CONFIGURACIÓN GENERAL Y TIEMPOS (VARIABLES CONFIGURABLES)
// ============================================================================
const char *ssid = "eddu";          // Nombre de red Wi-Fi
const char *password = "linux1234"; // Contraseña Wi-Fi

// TIEMPOS DE GIRO DEL MOTOR DE LA COCHERA (Subir y Bajar configurados por separado)
const unsigned long TIEMPO_COCHERA_SUBIR_SEGUNDOS = 14; // <-- TIEMPO PARA SUBIR / ABRIR (en segundos)
const unsigned long TIEMPO_COCHERA_BAJAR_SEGUNDOS = 13; // <-- TIEMPO PARA BAJAR / CERRAR (en segundos)
const unsigned long DURACION_COCHERA_SUBIR_MS = TIEMPO_COCHERA_SUBIR_SEGUNDOS * 1000UL;
const unsigned long DURACION_COCHERA_BAJAR_MS = TIEMPO_COCHERA_BAJAR_SEGUNDOS * 1000UL;

// TIEMPO DE APERTURA DEL SERVO DE LA PUERTA PRINCIPAL (en segundos)
const unsigned long TIEMPO_SERVO_PUERTA_SEGUNDOS = 3; // <-- AJUSTA AQUÍ LOS SEGUNDOS DE LA PUERTA
const unsigned long DURACION_SERVO_PUERTA_MS = TIEMPO_SERVO_PUERTA_SEGUNDOS * 1000UL;

// Cámara 1: Raspberry Pi Zero W (Webcam USB HD con mDNS)
const char *cam_rpi_stream = "http://camara-edificio.local:8080/stream";
const char *cam_rpi_panel = "http://camara-edificio.local:8080/";
const char *cam_rpi_fallback = "http://10.217.141.3:8080/stream";

// Cámara 2: ESP32-CAM (Módulo OV2640)
const char *cam_esp_stream = "http://10.217.141.107:81/stream";
const char *cam_esp_panel = "http://10.217.141.107/";
const char *cam_esp_fallback = "http://10.217.141.107/stream";

// Red de Respaldo Local (Punto de Acceso Autónomo si no hay router)
const char *ap_ssid = "Edificio-Inteligente";
const char *ap_pass = "12345678";

// ============================================================================
// 2. MAPA DE PINES Y CONSTANTES DE HARDWARE
// ============================================================================
// LCD I2C 16x2
#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 22
#define LCD_I2C_ADDR 0x27
#define LCD_COLS 16
#define LCD_ROWS 2

// Lector RFID RC522 (SPI)
#define PIN_RC522_SS 5
#define PIN_RC522_RST 4
#define PIN_RC522_SCK 18
#define PIN_RC522_MISO 19
#define PIN_RC522_MOSI 23

// Seguridad y Actuadores
#define PIN_REED_PUERTA 27  // LOW = Cerrada (imán), HIGH = Abierta
#define PIN_BUZZER 13       // Buzzer pasivo (tonos por frecuencia)
#define PIN_SERVO_PUERTA 25 // Servomotor de la Puerta Principal

// Motor Paso a Paso Cochera (Driver ULN2003 / 28BYJ-48)
#define PIN_STEPPER_IN1 14
#define PIN_STEPPER_IN2 12
#define PIN_STEPPER_IN3 16
#define PIN_STEPPER_IN4 17

// Módulo 4 Relés (Active-LOW: LOW = Encendido, HIGH = Apagado)
#define PIN_LUZ_DPTO1 26
#define PIN_LUZ_DPTO2 32
#define PIN_LUZ_DPTO3 33

#define RELE_ENCENDIDO LOW
#define RELE_APAGADO HIGH

// Parámetros Servomotor Puerta
#define SERVO_PUERTA_CERRADA 0
#define SERVO_PUERTA_ABIERTA 90

// ============================================================================
// 3. ESTRUCTURA Y BASE DE DATOS DE RESIDENTES (WHITELIST RFID)
// ============================================================================
struct UsuarioAutorizado
{
  byte uid[4];
  const char *nombre;
};

const UsuarioAutorizado RESIDENTES[] = {
    {{0x80, 0xE7, 0xEC, 0xDB}, "Dpto. 1"},
    {{0x52, 0xDA, 0x20, 0xF1}, "Dpto. 2"},
    {{0x19, 0x12, 0x6B, 0x06}, "Dpto. 3"}};
const int TOTAL_RESIDENTES = sizeof(RESIDENTES) / sizeof(RESIDENTES[0]);

// ============================================================================
// 4. MÁQUINA DE ESTADOS FINITOS (FSM) DEL SISTEMA
// ============================================================================
enum EstadoSistema
{
  ESTADO_CERRADO,           // Reposo: Puerta cerrada, vigilando RFID y sensor
  ESTADO_ACCESO_PERMITIDO,  // RFID válido: Puerta abierta por servo durante 3s
  ESTADO_PUERTA_ABIERTA_OK, // Puerta abierta físicamente tras autorización
  ESTADO_ALARMA             // Intrusión: Puerta abierta sin tarjeta autorizada
};

EstadoSistema estadoActual = ESTADO_CERRADO;

// ============================================================================
// 5. MOTOR PASO A PASO (COCHERA) - CONSTANTES Y VARIABLES
// ============================================================================
// Secuencia paso completo doble bobina (máximo torque)
const int SECUENCIA_STEPPER[4][4] = {
    {HIGH, HIGH, LOW, LOW},
    {LOW, HIGH, HIGH, LOW},
    {LOW, LOW, HIGH, HIGH},
    {HIGH, LOW, LOW, HIGH}};

enum EstadoCochera
{
  COCHERA_CERRADA,
  COCHERA_ABRIENDO,
  COCHERA_ABIERTA,
  COCHERA_CERRANDO
};

EstadoCochera estadoCochera = COCHERA_CERRADA;
unsigned long tiempoInicioCocheraMs = 0;

int direccionStepper = 0; // -1 = Antihorario (Abre), 1 = Horario (Cierra)
unsigned long ultimoPasoStepperMicros = 0;
const unsigned long INTERVALO_PASO_STEPPER_MICROS = 2600; // Velocidad óptima (~11 RPM)
int indicePasoStepper = 0;

// ============================================================================
// 6. SERVOMOTOR DE LA PUERTA PRINCIPAL
// ============================================================================
Servo servoPuerta;
bool servoPuertaAbierto = false;
unsigned long tiempoAperturaServoPuertaMs = 0;

// ============================================================================
// 7. BUZZER PASIVO - SISTEMA DE TONOS
// ============================================================================
enum ModoBuzzer
{
  BUZ_SILENCIO,
  BUZ_APROBATORIO,    // Melodía ascendente de confirmación (éxito)
  BUZ_DESAPROBATORIO, // Tono grave doble de error (rechazo)
  BUZ_ALARMA          // Sirena de emergencia oscilante
};

ModoBuzzer modoBuzzer = BUZ_SILENCIO;
unsigned long buzzerUltimoCambioMs = 0;
int buzzerPaso = 0;

// ============================================================================
// 8. OTRAS VARIABLES GLOBALES Y CONTROL
// ============================================================================
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
MFRC522 rfid(PIN_RC522_SS, PIN_RC522_RST);
WebServer server(80);

// Temporizador ventana FSM
unsigned long tiempoVentanaAcceso = 0;
const unsigned long DURACION_VENTANA_ACCESO_MS = 6000;

// Manejo Acceso Denegado
bool accesoDenegadoActivo = false;
unsigned long tiempoInicioDenegado = 0;
const unsigned long DURACION_DENEGADO_MS = 2000;

// Sensor Magnético con Debounce
int estadoPuertaActual = LOW; // LOW = Cerrada, HIGH = Abierta
int ultimoEstadoLecturaReed = LOW;
unsigned long ultimoDebounceReedMs = 0;
const unsigned long TIEMPO_DEBOUNCE_MS = 50;

// Relés de iluminación
bool estadoLuz1 = false;
bool estadoLuz2 = false;
bool estadoLuz3 = false;

// Registro de eventos
String ultimoResidente = "Ninguno";
String ultimoUid = "Ninguno";

// Buffer LCD para evitar parpadeos
char lcdBufferL1[17] = "";
char lcdBufferL2[17] = "";
unsigned long rfidUltimoEscaneoMs = 0;
const unsigned long INTERVALO_RFID_MS = 70;

// ============================================================================
// 9. PROTOTIPOS DE FUNCIONES
// ============================================================================
void inicializarPines();
void inicializarLCD();
void inicializarRFID();
void inicializarServo();
void inicializarStepper();
void inicializarWiFi();
void configurarServidorWeb();

void actualizarSensorPuerta(unsigned long ahora);
void actualizarRFID(unsigned long ahora);
void actualizarFSM(unsigned long ahora);
void actualizarBuzzer(unsigned long ahora);
void actualizarServoPuerta(unsigned long ahora);
void actualizarStepper(unsigned long ahoraMicros, unsigned long ahoraMillis);
void refrescarPantallaLCD();

void dispararBuzzer(ModoBuzzer modo);
void abrirPuertaServo();
void cerrarPuertaServo();
void iniciarAperturaCochera();
void iniciarCierreCochera();
void detenerCochera();
void desenergizarBobinasStepper();
void aplicarPasoStepper(int paso);
void desarmarAlarma(const char *motivo);
void setLuzDpto(int dpto, bool encender);
void redirigirRaiz();

int buscarResidente(byte *uid, byte uidSize);
String formatearUID(byte *uid, byte uidSize);

// Handlers Web
void handleRoot();
void handleStatusApi();
void handleLuz1On();
void handleLuz1Off();
void handleLuz2On();
void handleLuz2Off();
void handleLuz3On();
void handleLuz3Off();
void handleCocheraAbrir();
void handleCocheraCerrar();
void handleCocheraDetener();
void handlePuertaAbrir();
void handleAlarmaReset();
void handleNotFound();

// ============================================================================
// 10. SETUP
// ============================================================================
void setup()
{
  Serial.begin(115200);
  Serial.println(F("\n=================================================="));
  Serial.println(F("   EDIFICIO RESIDENCIAL INTELIGENTE IoT - ESP32   "));
  Serial.println(F("=================================================="));

  inicializarPines();
  inicializarLCD();
  inicializarRFID();
  inicializarServo();
  inicializarStepper();
  inicializarWiFi();
  configurarServidorWeb();

  dispararBuzzer(BUZ_APROBATORIO);
  Serial.println(F("[SISTEMA] Inicializado correctamente en ESTADO_CERRADO."));
}

// ============================================================================
// 11. LOOP PRINCIPAL (100% NO BLOQUEANTE)
// ============================================================================
void loop()
{
  unsigned long ahoraMillis = millis();
  unsigned long ahoraMicros = micros();

  actualizarSensorPuerta(ahoraMillis);
  actualizarRFID(ahoraMillis);
  actualizarFSM(ahoraMillis);
  actualizarBuzzer(ahoraMillis);
  actualizarServoPuerta(ahoraMillis);
  actualizarStepper(ahoraMicros, ahoraMillis);
  refrescarPantallaLCD();
  server.handleClient();
}

// ============================================================================
// 12. INICIALIZACIÓN DE HARDWARE Y PERIFÉRICOS
// ============================================================================
void inicializarPines()
{
  // Sensor Magnético Puerta (Reed Switch)
  pinMode(PIN_REED_PUERTA, INPUT_PULLUP);
  estadoPuertaActual = digitalRead(PIN_REED_PUERTA);
  ultimoEstadoLecturaReed = estadoPuertaActual;

  // Buzzer Pasivo
  pinMode(PIN_BUZZER, OUTPUT);
  noTone(PIN_BUZZER);

  // Módulo 4 Relés (Active-LOW) - HIGH para asegurar apagado en arranque
  digitalWrite(PIN_LUZ_DPTO1, RELE_APAGADO);
  digitalWrite(PIN_LUZ_DPTO2, RELE_APAGADO);
  digitalWrite(PIN_LUZ_DPTO3, RELE_APAGADO);

  pinMode(PIN_LUZ_DPTO1, OUTPUT);
  pinMode(PIN_LUZ_DPTO2, OUTPUT);
  pinMode(PIN_LUZ_DPTO3, OUTPUT);
}

void inicializarLCD()
{
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  lcd.init();
  lcd.backlight();
  lcd.clear();
}

void inicializarRFID()
{
  SPI.begin(PIN_RC522_SCK, PIN_RC522_MISO, PIN_RC522_MOSI, PIN_RC522_SS);
  rfid.PCD_Init();
  Serial.println(F("[RFID] MFRC522 listo en SPI."));
}

void inicializarServo()
{
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  servoPuerta.setPeriodHertz(50);
  servoPuerta.attach(PIN_SERVO_PUERTA, 500, 2400);
  cerrarPuertaServo();
}

void inicializarStepper()
{
  pinMode(PIN_STEPPER_IN1, OUTPUT);
  pinMode(PIN_STEPPER_IN2, OUTPUT);
  pinMode(PIN_STEPPER_IN3, OUTPUT);
  pinMode(PIN_STEPPER_IN4, OUTPUT);
  desenergizarBobinasStepper();
  Serial.println(F("[STEPPER] Motor paso a paso cochera configurado en GPIO 14, 12, 16, 17."));
}

void inicializarWiFi()
{
  WiFi.mode(WIFI_AP_STA);

  // Iniciar Punto de Acceso Local de respaldo
  WiFi.softAP(ap_ssid, ap_pass);
  Serial.print(F("[WIFI] Punto de Acceso Local iniciado. SSID: "));
  Serial.print(ap_ssid);
  Serial.print(F(" | IP AP: http://"));
  Serial.println(WiFi.softAPIP());

  // Conectar a la red configurada
  Serial.print(F("[WIFI] Conectando a red: "));
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0 < 2500))
  {
    delay(50);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print(F("[WIFI] Conectado exitosamente. IP STA: http://"));
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println(F("[WIFI] No conectado a STA. Operando en modo AP local autonomo."));
  }
}

// ============================================================================
// 13. SENSOR MAGNÉTICO Y RFID
// ============================================================================
void actualizarSensorPuerta(unsigned long ahora)
{
  int lectura = digitalRead(PIN_REED_PUERTA);

  if (lectura != ultimoEstadoLecturaReed)
  {
    ultimoDebounceReedMs = ahora;
    ultimoEstadoLecturaReed = lectura;
  }

  if ((ahora - ultimoDebounceReedMs) >= TIEMPO_DEBOUNCE_MS)
  {
    if (lectura != estadoPuertaActual)
    {
      estadoPuertaActual = lectura;
      if (estadoPuertaActual == HIGH)
      {
        Serial.println(F("[SENSOR] Puerta: ABIERTA (HIGH)"));
      }
      else
      {
        Serial.println(F("[SENSOR] Puerta: CERRADA (LOW)"));
      }
    }
  }
}

void actualizarRFID(unsigned long ahora)
{
  if (ahora - rfidUltimoEscaneoMs < INTERVALO_RFID_MS)
    return;
  rfidUltimoEscaneoMs = ahora;

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial())
  {
    return;
  }

  String uidStr = formatearUID(rfid.uid.uidByte, rfid.uid.size);
  int idx = buscarResidente(rfid.uid.uidByte, rfid.uid.size);
  ultimoUid = uidStr;

  if (idx >= 0)
  {
    // === TARJETA AUTORIZADA ===
    ultimoResidente = RESIDENTES[idx].nombre;
    Serial.print(F("[RFID] AUTORIZADO: "));
    Serial.print(ultimoResidente);
    Serial.print(F(" | UID: "));
    Serial.println(uidStr);

    accesoDenegadoActivo = false;

    // Si había alarma activa, se desarma
    if (estadoActual == ESTADO_ALARMA)
    {
      desarmarAlarma("Tarjeta RFID de residente");
    }

    // Transición a ESTADO_ACCESO_PERMITIDO
    estadoActual = ESTADO_ACCESO_PERMITIDO;
    tiempoVentanaAcceso = ahora;

    // 1. Tono aprobatorio en buzzer pasivo
    dispararBuzzer(BUZ_APROBATORIO);

    // 2. Abrir la puerta con el SERVOMOTOR (90°) por 3 segundos
    abrirPuertaServo();
  }
  else
  {
    // === TARJETA DENEGADA ===
    ultimoResidente = "Desconocido";
    Serial.print(F("[RFID] DENEGADO - UID no registrado: "));
    Serial.println(uidStr);

    accesoDenegadoActivo = true;
    tiempoInicioDenegado = ahora;

    // Tono de error en buzzer pasivo
    dispararBuzzer(BUZ_DESAPROBATORIO);
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ============================================================================
// 14. MÁQUINA DE ESTADOS FINITOS (FSM)
// ============================================================================
void actualizarFSM(unsigned long ahora)
{
  // Temporizador para mensaje de acceso denegado (2 segundos)
  if (accesoDenegadoActivo && (ahora - tiempoInicioDenegado >= DURACION_DENEGADO_MS))
  {
    accesoDenegadoActivo = false;
  }

  switch (estadoActual)
  {

  // ESTADO 1: REPOSO (CERRADO)
  case ESTADO_CERRADO:
    // Apertura no autorizada detectada por sensor magnético -> ALARMA
    if (estadoPuertaActual == HIGH)
    {
      Serial.println(F("[FSM] ¡¡INTRUSIÓN!! Puerta abierta sin autorización previa."));
      estadoActual = ESTADO_ALARMA;
      dispararBuzzer(BUZ_ALARMA);
    }
    break;

  // ESTADO 2: ACCESO PERMITIDO (VENTANA DE TIEMPO)
  case ESTADO_ACCESO_PERMITIDO:
    // Si la puerta física se abre
    if (estadoPuertaActual == HIGH)
    {
      Serial.println(F("[FSM] Puerta física abierta tras acceso autorizado."));
      estadoActual = ESTADO_PUERTA_ABIERTA_OK;
    }
    // Si expira la ventana sin abrir la puerta física y el servo ya cerró
    else if (!servoPuertaAbierto && (ahora - tiempoVentanaAcceso >= DURACION_VENTANA_ACCESO_MS))
    {
      Serial.println(F("[FSM] Ventana expirada. Retorno a ESTADO_CERRADO."));
      estadoActual = ESTADO_CERRADO;
    }
    break;

  // ESTADO 3: PUERTA ABIERTA AUTORIZADA
  case ESTADO_PUERTA_ABIERTA_OK:
    // Cuando la puerta física se vuelve a cerrar (sensor en LOW) y el servo cerró
    if (estadoPuertaActual == LOW && !servoPuertaAbierto)
    {
      Serial.println(F("[FSM] Puerta cerrada físicamente. Retorno a ESTADO_CERRADO."));
      estadoActual = ESTADO_CERRADO;
    }
    break;

  // ESTADO 4: ALARMA (INTRUSIÓN / PUERTA FORZADA)
  case ESTADO_ALARMA:
    // Se mantiene en alarma hasta que:
    // 1) La puerta física se cierre (sensor en LOW)
    // 2) O se pase una tarjeta autorizada
    // 3) O se presione silenciar en la Web
    if (estadoPuertaActual == LOW)
    {
      Serial.println(F("[FSM] Puerta cerrada. Alarma desactivada -> ESTADO_CERRADO."));
      desarmarAlarma("Puerta cerrada físicamente");
    }
    break;
  }
}

void desarmarAlarma(const char *motivo)
{
  Serial.print(F("[SEGURIDAD] Alarma desarmada: "));
  Serial.println(motivo);
  dispararBuzzer(BUZ_SILENCIO);
  if (estadoPuertaActual == HIGH)
  {
    estadoActual = ESTADO_PUERTA_ABIERTA_OK;
  }
  else
  {
    estadoActual = ESTADO_CERRADO;
  }
}

// ============================================================================
// 15. SERVOMOTOR DE LA PUERTA (APERTURA 3 SEGUNDOS)
// ============================================================================
void abrirPuertaServo()
{
  servoPuerta.write(SERVO_PUERTA_ABIERTA);
  servoPuertaAbierto = true;
  tiempoAperturaServoPuertaMs = millis();
  Serial.println(F("[SERVO PUERTA] Abierta a 90° (temporizador 3s iniciado)."));
}

void cerrarPuertaServo()
{
  servoPuerta.write(SERVO_PUERTA_CERRADA);
  servoPuertaAbierto = false;
  Serial.println(F("[SERVO PUERTA] Cerrada a 0°."));
}

void actualizarServoPuerta(unsigned long ahora)
{
  if (servoPuertaAbierto)
  {
    if (ahora - tiempoAperturaServoPuertaMs >= DURACION_SERVO_PUERTA_MS)
    {
      cerrarPuertaServo();
    }
  }
}

// ============================================================================
// 16. MOTOR PASO A PASO (COCHERA) - CONTROL ASÍNCRONO NO BLOQUEANTE
// ============================================================================
void desenergizarBobinasStepper()
{
  digitalWrite(PIN_STEPPER_IN1, LOW);
  digitalWrite(PIN_STEPPER_IN2, LOW);
  digitalWrite(PIN_STEPPER_IN3, LOW);
  digitalWrite(PIN_STEPPER_IN4, LOW);
}

void aplicarPasoStepper(int paso)
{
  digitalWrite(PIN_STEPPER_IN1, SECUENCIA_STEPPER[paso][0]);
  digitalWrite(PIN_STEPPER_IN2, SECUENCIA_STEPPER[paso][1]);
  digitalWrite(PIN_STEPPER_IN3, SECUENCIA_STEPPER[paso][2]);
  digitalWrite(PIN_STEPPER_IN4, SECUENCIA_STEPPER[paso][3]);
}

void iniciarAperturaCochera()
{
  estadoCochera = COCHERA_ABRIENDO;
  direccionStepper = -1; // Sentido antihorario = Abre (Sube)
  tiempoInicioCocheraMs = millis();
  Serial.printf("[COCHERA] Abriendo porton (Giro Antihorario por %lu segundos)...\n", TIEMPO_COCHERA_SUBIR_SEGUNDOS);
}

void iniciarCierreCochera()
{
  estadoCochera = COCHERA_CERRANDO;
  direccionStepper = 1; // Sentido horario = Cierra (Baja)
  tiempoInicioCocheraMs = millis();
  Serial.printf("[COCHERA] Cerrando porton (Giro Horario por %lu segundos)...\n", TIEMPO_COCHERA_BAJAR_SEGUNDOS);
}

void detenerCochera()
{
  desenergizarBobinasStepper();
  if (estadoCochera == COCHERA_ABRIENDO)
    estadoCochera = COCHERA_ABIERTA;
  else if (estadoCochera == COCHERA_CERRANDO)
    estadoCochera = COCHERA_CERRADA;
  Serial.println(F("[COCHERA] Movimiento detenido manualmente."));
}

void actualizarStepper(unsigned long ahoraMicros, unsigned long ahoraMillis)
{
  if (estadoCochera == COCHERA_ABRIENDO || estadoCochera == COCHERA_CERRANDO)
  {
    // Determinar la duración según si está subiendo o bajando
    unsigned long duracionObjetivoMs = (estadoCochera == COCHERA_ABRIENDO) ? DURACION_COCHERA_SUBIR_MS : DURACION_COCHERA_BAJAR_MS;

    // Verificar si se completó el tiempo correspondiente
    if (ahoraMillis - tiempoInicioCocheraMs >= duracionObjetivoMs)
    {
      if (estadoCochera == COCHERA_ABRIENDO)
      {
        estadoCochera = COCHERA_ABIERTA;
        Serial.println(F("[COCHERA] Fin de ciclo: Cochera ABIERTA."));
      }
      else
      {
        estadoCochera = COCHERA_CERRADA;
        Serial.println(F("[COCHERA] Fin de ciclo: Cochera CERRADA."));
      }
      desenergizarBobinasStepper();
      return;
    }

    // Ejecutar pasos según el intervalo de microsegundos
    if (ahoraMicros - ultimoPasoStepperMicros >= INTERVALO_PASO_STEPPER_MICROS)
    {
      ultimoPasoStepperMicros = ahoraMicros;
      if (direccionStepper == 1)
      {
        indicePasoStepper = (indicePasoStepper + 1) % 4;
      }
      else
      {
        indicePasoStepper = (indicePasoStepper - 1 + 4) % 4;
      }
      aplicarPasoStepper(indicePasoStepper);
    }
  }
}

// ============================================================================
// 17. BUZZER PASIVO - GENERACIÓN DE TONOS NO BLOQUEANTE
// ============================================================================
void dispararBuzzer(ModoBuzzer modo)
{
  modoBuzzer = modo;
  buzzerPaso = 0;
  buzzerUltimoCambioMs = millis();

  if (modo == BUZ_SILENCIO)
  {
    noTone(PIN_BUZZER);
  }
  else if (modo == BUZ_APROBATORIO)
  {
    tone(PIN_BUZZER, 1760); // Tono 1 (La6 / A6)
  }
  else if (modo == BUZ_DESAPROBATORIO)
  {
    tone(PIN_BUZZER, 400); // Tono grave 1
  }
  else if (modo == BUZ_ALARMA)
  {
    tone(PIN_BUZZER, 850); // Frecuencia inicial sirena
  }
}

void actualizarBuzzer(unsigned long ahora)
{
  switch (modoBuzzer)
  {
  case BUZ_SILENCIO:
    break;

  case BUZ_APROBATORIO:
    // Tono 1: 1760 Hz (100ms) -> Tono 2: 2637 Hz (150ms) -> Silencio
    if (buzzerPaso == 0 && (ahora - buzzerUltimoCambioMs >= 100))
    {
      buzzerPaso = 1;
      buzzerUltimoCambioMs = ahora;
      tone(PIN_BUZZER, 2637); // Tono 2 (Mi7 / E7)
    }
    else if (buzzerPaso == 1 && (ahora - buzzerUltimoCambioMs >= 150))
    {
      noTone(PIN_BUZZER);
      modoBuzzer = BUZ_SILENCIO;
    }
    break;

  case BUZ_DESAPROBATORIO:
    // Tono grave 1: 400 Hz (150ms) -> Pausa 70ms -> Tono grave 2: 300 Hz (220ms) -> Silencio
    if (buzzerPaso == 0 && (ahora - buzzerUltimoCambioMs >= 150))
    {
      buzzerPaso = 1;
      buzzerUltimoCambioMs = ahora;
      noTone(PIN_BUZZER);
    }
    else if (buzzerPaso == 1 && (ahora - buzzerUltimoCambioMs >= 70))
    {
      buzzerPaso = 2;
      buzzerUltimoCambioMs = ahora;
      tone(PIN_BUZZER, 300);
    }
    else if (buzzerPaso == 2 && (ahora - buzzerUltimoCambioMs >= 220))
    {
      noTone(PIN_BUZZER);
      modoBuzzer = BUZ_SILENCIO;
    }
    break;

  case BUZ_ALARMA:
    // Sirena oscilante: modula entre 850 Hz y 1900 Hz cada 160ms en bucle continuo
    if (ahora - buzzerUltimoCambioMs >= 160)
    {
      buzzerUltimoCambioMs = ahora;
      buzzerPaso = (buzzerPaso + 1) % 2;
      if (buzzerPaso == 0)
      {
        tone(PIN_BUZZER, 850);
      }
      else
      {
        tone(PIN_BUZZER, 1900);
      }
    }
    break;
  }
}

// ============================================================================
// 18. GESTIÓN DE PANTALLA LCD 16x2
// ============================================================================
void refrescarPantallaLCD()
{
  char l1[17] = "";
  char l2[17] = "";

  switch (estadoActual)
  {
  case ESTADO_CERRADO:
    if (accesoDenegadoActivo)
    {
      snprintf(l1, sizeof(l1), "%-16.16s", "Acceso Denegado");
      snprintf(l2, sizeof(l2), "%-16.16s", "No Autorizado");
    }
    else
    {
      snprintf(l1, sizeof(l1), "%-16.16s", "Edificio Seguro");
      snprintf(l2, sizeof(l2), "%-16.16s", "Acerque tarjeta");
    }
    break;

  case ESTADO_ACCESO_PERMITIDO:
  {
    snprintf(l1, sizeof(l1), "%-16.16s", "Acceso Permitido");
    char bienvenida[17];
    snprintf(bienvenida, sizeof(bienvenida), "Bienv. %s", ultimoResidente.c_str());
    snprintf(l2, sizeof(l2), "%-16.16s", bienvenida);
    break;
  }

  case ESTADO_PUERTA_ABIERTA_OK:
    snprintf(l1, sizeof(l1), "%-16.16s", "Puerta Abierta");
    snprintf(l2, sizeof(l2), "%-16.16s", "Pase adelante");
    break;

  case ESTADO_ALARMA:
    snprintf(l1, sizeof(l1), "%-16.16s", "!ALERTA SEGURID!");
    snprintf(l2, sizeof(l2), "%-16.16s", "Puerta Forzada");
    break;
  }

  // Refrescar en LCD únicamente si el texto ha cambiado
  if (strncmp(lcdBufferL1, l1, 16) != 0 || strncmp(lcdBufferL2, l2, 16) != 0)
  {
    strncpy(lcdBufferL1, l1, 16);
    strncpy(lcdBufferL2, l2, 16);
    lcdBufferL1[16] = '\0';
    lcdBufferL2[16] = '\0';

    lcd.setCursor(0, 0);
    lcd.print(lcdBufferL1);
    lcd.setCursor(0, 1);
    lcd.print(lcdBufferL2);
  }
}

// ============================================================================
// 19. UTILIDADES Y CONTROL DE ILUMINACIÓN
// ============================================================================
void setLuzDpto(int dpto, bool encender)
{
  uint8_t estado = encender ? RELE_ENCENDIDO : RELE_APAGADO;
  switch (dpto)
  {
  case 1:
    estadoLuz1 = encender;
    digitalWrite(PIN_LUZ_DPTO1, estado);
    Serial.printf("[ILUMINACION] Luz Dpto 1: %s\n", encender ? "ENCENDIDA" : "APAGADA");
    break;
  case 2:
    estadoLuz2 = encender;
    digitalWrite(PIN_LUZ_DPTO2, estado);
    Serial.printf("[ILUMINACION] Luz Dpto 2: %s\n", encender ? "ENCENDIDA" : "APAGADA");
    break;
  case 3:
    estadoLuz3 = encender;
    digitalWrite(PIN_LUZ_DPTO3, estado);
    Serial.printf("[ILUMINACION] Luz Dpto 3: %s\n", encender ? "ENCENDIDA" : "APAGADA");
    break;
  }
}

void redirigirRaiz()
{
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

int buscarResidente(byte *uid, byte uidSize)
{
  if (uidSize != 4)
    return -1;
  for (int i = 0; i < TOTAL_RESIDENTES; i++)
  {
    if (memcmp(uid, RESIDENTES[i].uid, 4) == 0)
    {
      return i;
    }
  }
  return -1;
}

String formatearUID(byte *uid, byte uidSize)
{
  String salida = "";
  for (byte i = 0; i < uidSize; i++)
  {
    if (uid[i] < 0x10)
      salida += "0";
    salida += String(uid[i], HEX);
    if (i < uidSize - 1)
      salida += ":";
  }
  salida.toUpperCase();
  return salida;
}

// ============================================================================
// 20. CONFIGURACIÓN DEL SERVIDOR WEB HTTP (PUERTO 80)
// ============================================================================
void configurarServidorWeb()
{
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatusApi);

  // Iluminación
  server.on("/luz1/on", HTTP_ANY, handleLuz1On);
  server.on("/luz1/off", HTTP_ANY, handleLuz1Off);
  server.on("/luz2/on", HTTP_ANY, handleLuz2On);
  server.on("/luz2/off", HTTP_ANY, handleLuz2Off);
  server.on("/luz3/on", HTTP_ANY, handleLuz3On);
  server.on("/luz3/off", HTTP_ANY, handleLuz3Off);

  // Cochera (Motor Paso a Paso)
  server.on("/cochera/abrir", HTTP_ANY, handleCocheraAbrir);
  server.on("/cochera/cerrar", HTTP_ANY, handleCocheraCerrar);
  server.on("/cochera/detener", HTTP_ANY, handleCocheraDetener);

  // Puerta Principal (Servomotor)
  server.on("/puerta/abrir", HTTP_ANY, handlePuertaAbrir);

  // Alarma
  server.on("/alarma/reset", HTTP_ANY, handleAlarmaReset);

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println(F("[HTTP] Servidor Web listo en el puerto 80."));
}

void handleLuz1On()
{
  setLuzDpto(1, true);
  redirigirRaiz();
}
void handleLuz1Off()
{
  setLuzDpto(1, false);
  redirigirRaiz();
}
void handleLuz2On()
{
  setLuzDpto(2, true);
  redirigirRaiz();
}
void handleLuz2Off()
{
  setLuzDpto(2, false);
  redirigirRaiz();
}
void handleLuz3On()
{
  setLuzDpto(3, true);
  redirigirRaiz();
}
void handleLuz3Off()
{
  setLuzDpto(3, false);
  redirigirRaiz();
}

void handleCocheraAbrir()
{
  iniciarAperturaCochera();
  redirigirRaiz();
}
void handleCocheraCerrar()
{
  iniciarCierreCochera();
  redirigirRaiz();
}
void handleCocheraDetener()
{
  detenerCochera();
  redirigirRaiz();
}

void handlePuertaAbrir()
{
  abrirPuertaServo();
  redirigirRaiz();
}
void handleAlarmaReset()
{
  desarmarAlarma("Reset desde la Web");
  redirigirRaiz();
}

void handleNotFound()
{
  server.send(404, "text/plain", "Recurso no encontrado en ESP32.");
}

// Endpoint JSON consumido en tiempo real por el script del Dashboard
void handleStatusApi()
{
  const char *estadoFsmStr = "CERRADO";
  switch (estadoActual)
  {
  case ESTADO_CERRADO:
    estadoFsmStr = "CERRADO";
    break;
  case ESTADO_ACCESO_PERMITIDO:
    estadoFsmStr = "ACCESO_PERMITIDO";
    break;
  case ESTADO_PUERTA_ABIERTA_OK:
    estadoFsmStr = "PUERTA_ABIERTA_OK";
    break;
  case ESTADO_ALARMA:
    estadoFsmStr = "ALARMA";
    break;
  }

  const char *estadoCocheraStr = "Cerrada";
  switch (estadoCochera)
  {
  case COCHERA_CERRADA:
    estadoCocheraStr = "Cerrada";
    break;
  case COCHERA_ABRIENDO:
    estadoCocheraStr = "Abriendo... (Antihorario)";
    break;
  case COCHERA_ABIERTA:
    estadoCocheraStr = "Abierta";
    break;
  case COCHERA_CERRANDO:
    estadoCocheraStr = "Cerrando... (Horario)";
    break;
  }

  String json = "{";
  json += "\"puerta_abierta\":" + String(estadoPuertaActual == HIGH ? "true" : "false") + ",";
  json += "\"servo_puerta_abierto\":" + String(servoPuertaAbierto ? "true" : "false") + ",";
  json += "\"estado_fsm\":\"" + String(estadoFsmStr) + "\",";
  json += "\"alarma_activa\":" + String(estadoActual == ESTADO_ALARMA ? "true" : "false") + ",";
  json += "\"cochera_estado\":\"" + String(estadoCocheraStr) + "\",";
  json += "\"luz1\":" + String(estadoLuz1 ? "true" : "false") + ",";
  json += "\"luz2\":" + String(estadoLuz2 ? "true" : "false") + ",";
  json += "\"luz3\":" + String(estadoLuz3 ? "true" : "false") + ",";
  json += "\"ultimo_residente\":\"" + ultimoResidente + "\",";
  json += "\"ultimo_uid\":\"" + ultimoUid + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

// ============================================================================
// 21. DASHBOARD WEB DINÁMICO EN TIEMPO REAL
// ============================================================================
void handleRoot()
{
  String html = "<!DOCTYPE html><html lang='es'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1.0'>";
  html += "<title>Control Residencial IoT</title>";
  html += "<style>";
  html += ":root{--bg:#0f172a;--card:#1e293b;--border:#334155;--text:#f8fafc;--muted:#94a3b8;--cyan:#38bdf8;--green:#22c55e;--red:#ef4444;--amber:#f59e0b;}";
  html += "*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;}";
  html += "body{background:var(--bg);color:var(--text);padding:1.5rem;min-height:100vh;}";
  html += ".wrap{max-width:900px;margin:0 auto;}";
  html += "header{text-align:center;margin-bottom:1.5rem;padding-bottom:1rem;border-bottom:1px solid var(--border);}";
  html += "h1{font-size:1.8rem;color:var(--cyan);margin-bottom:.3rem;}";
  html += ".banner-alarm{display:none;background:rgba(239,68,68,0.2);border:2px solid var(--red);color:#fca5a5;padding:1rem;border-radius:.75rem;margin-bottom:1.5rem;text-align:center;animation:blink 1s infinite;}";
  html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(270px,1fr));gap:1.25rem;margin-bottom:1.5rem;}";
  html += ".card{background:var(--card);border:1px solid var(--border);border-radius:1rem;padding:1.25rem;box-shadow:0 4px 6px -1px rgba(0,0,0,0.3);}";
  html += ".card h2{font-size:1.15rem;margin-bottom:1rem;display:flex;justify-content:space-between;align-items:center;}";
  html += ".row{display:flex;justify-content:space-between;align-items:center;margin-bottom:.8rem;font-size:.95rem;}";
  html += ".badge{padding:.3rem .8rem;border-radius:999px;font-size:.85rem;font-weight:700;}";
  html += ".badge-ok{background:rgba(34,197,94,.2);color:#4ade80;border:1px solid rgba(34,197,94,.4);}";
  html += ".badge-alarm{background:rgba(239,68,68,.25);color:#f87171;border:1px solid rgba(239,68,68,.5);animation:blink 1s infinite;}";
  html += ".badge-open{background:rgba(245,158,11,.2);color:#fbbf24;border:1px solid rgba(245,158,11,.4);}";
  html += "@keyframes blink{0%,100%{opacity:1}50%{opacity:.4}}";
  html += ".btn-group{display:flex;gap:.5rem;margin-top:.75rem;}";
  html += ".btn{flex:1;text-align:center;padding:.7rem .8rem;border-radius:.5rem;text-decoration:none;font-weight:600;font-size:.92rem;cursor:pointer;border:none;transition:transform .1s;}";
  html += ".btn:active{transform:scale(.98);}";
  html += ".btn-on{background:var(--green);color:#fff;}";
  html += ".btn-off{background:#475569;color:#cbd5e1;}";
  html += ".btn-primary{background:var(--cyan);color:#0f172a;}";
  html += ".btn-sec{background:#334155;color:#f8fafc;}";
  html += ".btn-red{background:var(--red);color:#fff;}";
  html += ".light-item{display:flex;justify-content:space-between;align-items:center;padding:.75rem 0;border-bottom:1px solid var(--border);}";
  html += ".light-item:last-child{border-bottom:none;}";
  html += ".cam-box{text-align:center;margin-top:1rem;background:#090d16;border-radius:.75rem;padding:.75rem;border:1px dashed var(--border);}";
  html += ".cam-stream{width:100%;max-width:540px;border-radius:.5rem;border:1px solid var(--border);display:block;margin:0 auto;}";
  html += "footer{text-align:center;color:var(--muted);font-size:.85rem;margin-top:2rem;}";
  html += "</style></head><body><div class='wrap'>";

  html += "<header><h1>Edificio Residencial Inteligente</h1><p style='color:var(--muted);'>Monitoreo y Control Autónomo en Tiempo Real</p></header>";

  // Banner Alarma de Intrusión
  html += "<div id='banner-alarma' class='banner-alarm'>";
  html += "<h2 style='color:#f87171;margin-bottom:.5rem;'>🚨 ¡¡ALERTA DE SEGURIDAD - PUERTA FORZADA!! 🚨</h2>";
  html += "<p style='margin-bottom:.8rem;'>Se ha detectado apertura no autorizada de la puerta de ingreso principal.</p>";
  html += "<a href='/alarma/reset' class='btn btn-red' style='display:inline-block;max-width:280px;'>🔴 Silenciar / Resetear Alarma</a>";
  html += "</div>";

  html += "<div class='grid'>";

  // TARJETA 1: SEGURIDAD PUERTA PRINCIPAL
  html += "<div class='card'>";
  html += "<h2>Puerta Principal <span id='badge-puerta' class='badge badge-ok'>Cerrada</span></h2>";
  html += "<div class='row'><span style='color:var(--muted);'>Sensor Magnético:</span><strong id='txt-puerta-sensor'>Cerrada (LOW)</strong></div>";
  html += "<div class='row'><span style='color:var(--muted);'>Cerrojo Servo (GPIO 25):</span><strong id='txt-puerta-servo'>Cerrado (0°)</strong></div>";
  html += "<div class='row'><span style='color:var(--muted);'>Último Residente:</span><strong id='txt-residente'>" + ultimoResidente + "</strong></div>";
  html += "<div class='row'><span style='color:var(--muted);'>Último UID:</span><code id='txt-uid' style='color:var(--cyan);'>" + ultimoUid + "</code></div>";
  html += "<div class='btn-group'>";
  html += "<a href='/puerta/abrir' class='btn btn-primary'>🔓 Abrir Puerta (" + String(TIEMPO_SERVO_PUERTA_SEGUNDOS) + "s)</a>";
  html += "<a id='btn-silenciar' href='/alarma/reset' class='btn btn-red' style='display:none;'>Silenciar</a>";
  html += "</div></div>";

  // TARJETA 2: COCHERA (MOTOR PASO A PASO)
  html += "<div class='card'>";
  html += "<h2>Cochera Común <span id='badge-cochera' class='badge badge-ok'>Cerrada</span></h2>";
  html += "<p style='color:var(--muted);font-size:.9rem;margin-bottom:.8rem;'>Motor Paso a Paso (Driver ULN2003):</p>";
  html += "<div class='row'><span style='color:var(--muted);'>Tiempos:</span><strong>Sube: " + String(TIEMPO_COCHERA_SUBIR_SEGUNDOS) + "s | Baja: " + String(TIEMPO_COCHERA_BAJAR_SEGUNDOS) + "s</strong></div>";
  html += "<div class='btn-group'>";
  html += "<a href='/cochera/abrir' class='btn btn-primary'>Abrir (" + String(TIEMPO_COCHERA_SUBIR_SEGUNDOS) + "s)</a>";
  html += "<a href='/cochera/cerrar' class='btn btn-sec'>Cerrar (" + String(TIEMPO_COCHERA_BAJAR_SEGUNDOS) + "s)</a>";
  html += "</div>";
  html += "<div style='margin-top:.5rem;'><a href='/cochera/detener' class='btn btn-sec' style='display:block;text-align:center;'>⏹ Detener Motor</a></div>";
  html += "</div>";

  // TARJETA 3: CONTROL DE ILUMINACIÓN DEPARTAMENTOS
  html += "<div class='card' style='grid-column:1/-1;'>";
  html += "<h2>Control de Iluminación Independiente</h2>";

  // Dpto 1
  html += "<div class='light-item'><div><strong>Dpto. 1</strong> <span style='font-size:.85rem;color:var(--muted);'>(GPIO 26)</span><br><span id='txt-luz1' style='font-size:.9rem;color:var(--muted);'>Apagada</span></div>";
  html += "<div class='btn-group' style='margin:0;max-width:200px;'><a href='/luz1/on' class='btn btn-on'>ON</a><a href='/luz1/off' class='btn btn-off'>OFF</a></div></div>";

  // Dpto 2
  html += "<div class='light-item'><div><strong>Dpto. 2</strong> <span style='font-size:.85rem;color:var(--muted);'>(GPIO 32)</span><br><span id='txt-luz2' style='font-size:.9rem;color:var(--muted);'>Apagada</span></div>";
  html += "<div class='btn-group' style='margin:0;max-width:200px;'><a href='/luz2/on' class='btn btn-on'>ON</a><a href='/luz2/off' class='btn btn-off'>OFF</a></div></div>";

  // Dpto 3
  html += "<div class='light-item'><div><strong>Dpto. 3</strong> <span style='font-size:.85rem;color:var(--muted);'>(GPIO 33)</span><br><span id='txt-luz3' style='font-size:.9rem;color:var(--muted);'>Apagada</span></div>";
  html += "<div class='btn-group' style='margin:0;max-width:200px;'><a href='/luz3/on' class='btn btn-on'>ON</a><a href='/luz3/off' class='btn btn-off'>OFF</a></div></div>";

  html += "</div>";

  // TARJETA 4: CÁMARAS DE VIGILANCIA (SISTEMA DUAL: RASPBERRY PI Y ESP32-CAM)
  html += "<div class='card' style='grid-column:1/-1;'>";
  html += "<h2>Vigilancia de Acceso &bull; Cámaras del Edificio</h2>";
  html += "<p style='color:var(--muted);font-size:.9rem;margin-bottom:.8rem;'>Selecciona la cámara que deseas visualizar en tiempo real:</p>";

  // Pestañas / Botones selectores
  html += "<div style='display:flex;gap:.5rem;margin-bottom:1rem;flex-wrap:wrap;'>";
  html += "<button type='button' id='tab-rpi' class='btn btn-primary' onclick=\"cambiarCam('rpi')\">📹 Webcam USB (Raspberry Pi)</button>";
  html += "<button type='button' id='tab-esp' class='btn btn-sec' onclick=\"cambiarCam('esp')\">📷 ESP32-CAM (OV2640)</button>";
  html += "<button type='button' id='tab-both' class='btn btn-sec' onclick=\"cambiarCam('both')\">👥 Ver Ambas (Mosaico)</button>";
  html += "</div>";

  // Contenedor 1: Raspberry Pi Zero W (Webcam USB)
  html += "<div id='box-cam-rpi' class='cam-box' style='margin-bottom:1rem;'>";
  html += "<div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:.5rem;'>";
  html += "<span style='color:var(--cyan);font-weight:600;font-size:.95rem;'>Cámara Principal: Webcam HD (Raspberry Pi)</span>";
  html += "<a href='" + String(cam_rpi_panel) + "' target='_blank' style='font-size:.85rem;color:var(--cyan);text-decoration:none;'>[Panel RPi &rarr;]</a>";
  html += "</div>";
  html += "<img class='cam-stream' id='img-cam-rpi' src='" + String(cam_rpi_stream) + "' alt='Stream Webcam RPi' ";
  html += "onerror=\"if(!this.dataset.tried){this.dataset.tried=1; this.src='" + String(cam_rpi_fallback) + "';} else {this.style.display='none'; document.getElementById('aviso-rpi').style.display='block';}\">";
  html += "<div id='aviso-rpi' style='display:none;padding:1.5rem;color:var(--muted);font-size:.9rem;'>";
  html += "Stream RPi no disponible. Verifica que la Raspberry Pi esté encendida en <a href='" + String(cam_rpi_panel) + "' target='_blank' style='color:var(--cyan);'>" + String(cam_rpi_panel) + "</a>.";
  html += "</div></div>";

  // Contenedor 2: ESP32-CAM (OV2640)
  html += "<div id='box-cam-esp' class='cam-box' style='display:none;'>";
  html += "<div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:.5rem;'>";
  html += "<span style='color:var(--amber);font-weight:600;font-size:.95rem;'>Cámara Secundaria: ESP32-CAM</span>";
  html += "<a href='" + String(cam_esp_panel) + "' target='_blank' style='font-size:.85rem;color:var(--amber);text-decoration:none;'>[Ajustes ESP32-CAM &rarr;]</a>";
  html += "</div>";
  html += "<img class='cam-stream' id='img-cam-esp' src='' alt='Stream ESP32-CAM' ";
  html += "onerror=\"if(!this.dataset.tried){this.dataset.tried=1; this.src='" + String(cam_esp_fallback) + "';} else {this.style.display='none'; document.getElementById('aviso-esp').style.display='block';}\">";
  html += "<div id='aviso-esp' style='display:none;padding:1.5rem;color:var(--muted);font-size:.9rem;'>";
  html += "Stream ESP32-CAM no disponible. Verifica que esté encendida en <a href='" + String(cam_esp_panel) + "' target='_blank' style='color:var(--amber);'>" + String(cam_esp_panel) + "</a> y pulsa 'Start Stream'.";
  html += "</div></div>";

  html += "</div>"; // fin tarjeta

  html += "</div>"; // fin grid
  html += "<footer>ESP32 DevKit V1 &bull; FSM No Bloqueante &bull; Control Autónomo</footer>";

  // ==========================================================================
  // JAVASCRIPT: SONDEO AJAX EN VIVO CADA 1 SEGUNDO Y CONTROL DE CÁMARAS
  // ==========================================================================
  html += "<script>";
  html += "async function actualizarEstado(){";
  html += "  try{";
  html += "    const res = await fetch('/api/status');";
  html += "    const d = await res.json();";
  html += "    const banner = document.getElementById('banner-alarma');";
  html += "    const badgePuerta = document.getElementById('badge-puerta');";
  html += "    const btnSilenciar = document.getElementById('btn-silenciar');";
  html += "    const txtSensor = document.getElementById('txt-puerta-sensor');";
  html += "    const txtServo = document.getElementById('txt-puerta-servo');";
  html += "    txtSensor.innerText = d.puerta_abierta ? 'Abierta (HIGH)' : 'Cerrada (LOW)';";
  html += "    txtServo.innerText = d.servo_puerta_abierto ? 'Abierto (90°)' : 'Cerrado (0°)';";
  html += "    document.getElementById('txt-residente').innerText = d.ultimo_residente;";
  html += "    document.getElementById('txt-uid').innerText = d.ultimo_uid;";

  // Alarma en tiempo real
  html += "    if(d.alarma_activa){";
  html += "      banner.style.display = 'block';";
  html += "      badgePuerta.className = 'badge badge-alarm';";
  html += "      badgePuerta.innerText = '¡ALARMA INTRUSIÓN!';";
  html += "      btnSilenciar.style.display = 'inline-block';";
  html += "    } else {";
  html += "      banner.style.display = 'none';";
  html += "      btnSilenciar.style.display = 'none';";
  html += "      if(d.puerta_abierta){";
  html += "        badgePuerta.className = 'badge badge-open';";
  html += "        badgePuerta.innerText = 'Abierta';";
  html += "      } else {";
  html += "        badgePuerta.className = 'badge badge-ok';";
  html += "        badgePuerta.innerText = 'Cerrada';";
  html += "      }";
  html += "    }";

  // Estado Cochera
  html += "    const badgeCochera = document.getElementById('badge-cochera');";
  html += "    badgeCochera.innerText = d.cochera_estado;";
  html += "    if(d.cochera_estado === 'Abierta'){ badgeCochera.className = 'badge badge-open'; }";
  html += "    else if(d.cochera_estado === 'Cerrada'){ badgeCochera.className = 'badge badge-ok'; }";
  html += "    else { badgeCochera.className = 'badge badge-alarm'; }";

  // Luces
  html += "    ponerLuz('txt-luz1', d.luz1);";
  html += "    ponerLuz('txt-luz2', d.luz2);";
  html += "    ponerLuz('txt-luz3', d.luz3);";
  html += "  }catch(err){ console.error('Error sincronizando estado:', err); }";
  html += "}";

  html += "function ponerLuz(id, encendida){";
  html += "  const el = document.getElementById(id);";
  html += "  el.innerText = encendida ? 'Encendida' : 'Apagada';";
  html += "  el.style.color = encendida ? 'var(--green)' : 'var(--muted)';";
  html += "}";

  // Control de Cámaras
  html += "const URL_RPI = '" + String(cam_rpi_stream) + "';";
  html += "const URL_ESP = '" + String(cam_esp_stream) + "';";
  html += "function cambiarCam(modo){";
  html += "  const bRpi=document.getElementById('box-cam-rpi');";
  html += "  const bEsp=document.getElementById('box-cam-esp');";
  html += "  const iRpi=document.getElementById('img-cam-rpi');";
  html += "  const iEsp=document.getElementById('img-cam-esp');";
  html += "  const tRpi=document.getElementById('tab-rpi');";
  html += "  const tEsp=document.getElementById('tab-esp');";
  html += "  const tBoth=document.getElementById('tab-both');";
  html += "  tRpi.className='btn btn-sec'; tEsp.className='btn btn-sec'; tBoth.className='btn btn-sec';";
  html += "  if(modo==='rpi'){";
  html += "    tRpi.className='btn btn-primary'; bRpi.style.display='block'; bEsp.style.display='none';";
  html += "    if(!iRpi.src||iRpi.style.display==='none'){ iRpi.style.display='block'; iRpi.src=URL_RPI; }";
  html += "    iEsp.src='';";
  html += "  } else if(modo==='esp'){";
  html += "    tEsp.className='btn btn-primary'; bRpi.style.display='none'; bEsp.style.display='block';";
  html += "    if(!iEsp.src||iEsp.src===''||iEsp.style.display==='none'){ iEsp.style.display='block'; iEsp.src=URL_ESP; }";
  html += "    iRpi.src='';";
  html += "  } else if(modo==='both'){";
  html += "    tBoth.className='btn btn-primary'; bRpi.style.display='block'; bEsp.style.display='block';";
  html += "    iRpi.style.display='block'; iRpi.src=URL_RPI;";
  html += "    iEsp.style.display='block'; iEsp.src=URL_ESP;";
  html += "  }";
  html += "}";

  // Sondeo periódico cada 1 segundo (1000ms)
  html += "setInterval(actualizarEstado, 1000);";
  html += "window.onload = actualizarEstado;";
  html += "</script>";

  html += "</div></body></html>";

  server.send(200, "text/html; charset=utf-8", html);
}

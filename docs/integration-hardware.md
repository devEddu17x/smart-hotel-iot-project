# Guía de Integración — 4. Hardware (ESP32, ESP32-CAM y Raspberry Pi Zero)

Referencia técnica para conectar los dispositivos físicos con AWS IoT Core, API Gateway/S3 y Kinesis Video Streams.

---

## 1. Datos base del entorno AWS (`dev` | `qa` | `prod`)

- **Región**: `us-east-1`
- **IoT Data ATS Endpoint**: `aws iot describe-endpoint --endpoint-type iot:Data-ATS`
- **IoT Credentials Endpoint** *(para Raspberry Pi KVS)*: `aws iot describe-endpoint --endpoint-type iot:CredentialProvider`
- **Root CA de AWS IoT**: Amazon Root CA 1 (`AmazonRootCA1.pem`)

---

## 2. ESP32 DevKit (Controlador Principal)

### A. Identidad y Conexión MQTT
- **Protocolo y Puerto**: MQTT sobre **TLS 1.2 (mTLS)**, puerto **`8883`**.
- **Thing Name / Client ID obligatorio**: `smart-hotel-esp32-<env>` (la IoT Policy exige que el `clientId` coincida exactamente con este nombre).
- **Certificados**: Generar X.509 con `aws iot create-keys-and-certificate`, adjuntar al Thing (`smart-hotel-esp32-<env>`) y adjuntar la política `smart-hotel-esp32-policy-<env>`.

### B. Tópicos MQTT y Payloads

| Dirección | Tópico | Uso / Formato esperado |
| :--- | :--- | :--- |
| **Publish** | `building/<env>/events` | Eventos persistidos automáticamente en `EventsTable` por IoT Rule. Incluir `eventType` (ej. `"RFID_ACCESS"`, `"DOOR_FORCED"`), `departmentId`, `cardUid`, `allowed`, etc. *(La regla añade `buildingId="main"`, `timestamp` y `ttl`)*. |
| **Publish** | `building/<env>/telemetry` | Estado en vivo para Web/Android (no se guarda en DynamoDB). |
| **Subscribe** | `building/<env>/commands/garage` | `{"action": "open" \| "close" \| "stop", "userId": "...", "ts": 1700000000}` |
| **Subscribe** | `building/<env>/commands/door` | `{"action": "open", "userId": "...", "ts": 1700000000}` |
| **Subscribe** | `building/<env>/commands/alarm` | `{"action": "silence", "userId": "...", "ts": 1700000000}` |

*(Nota de seguridad: validar `ts` o ignorar comandos `garage`/`door` si llegan tras una desconexión prolongada).*

### C. Device Shadow (`smart-hotel-esp32-<env>`)
- **Subscribe**:
  - `$aws/things/smart-hotel-esp32-<env>/shadow/update/delta` (recibe cambios pendientes de luces).
  - `$aws/things/smart-hotel-esp32-<env>/shadow/get/accepted` (para sincronizar al reconectarse).
- **Publish**:
  - `$aws/things/smart-hotel-esp32-<env>/shadow/update`
  - `$aws/things/smart-hotel-esp32-<env>/shadow/get`
- **Estructura `state.desired` / `state.reported`**:
  - Luces por departamento: `{"lights": {"201": true, "301": false, "401": false}}`
  - Estado de cochera (reportado por ESP32 para el bloqueo de 1 orden a la vez en el backend):
    `"garage": "Cerrada" | "Abierta" | "opening" | "closing"` (el backend bloquea nuevas órdenes `open`/`close` mientras `reported.garage` sea `"opening"`, `"closing"`, `"Abriendo... (Antihorario)"` o `"Cerrando... (Horario)"`).

---

## 3. ESP32-CAM (Snapshots a S3)

La ESP32-CAM **no** usa MQTT ni Kinesis; sube imágenes JPEG por HTTPS usando Presigned URLs:

1. **Pedir URL de subida**:
   - `POST <ApiUrl>/camera/snapshots/upload-url`
   - Header obligatorio: `X-Device-Key: <DeviceApiKey>` (configurado en `ApiStack`)
   - Respuesta `200`: `{"key": "snapshots/20261120T120000Z.jpg", "uploadUrl": "https://..."}`
2. **Subir JPEG a S3**:
   - `PUT <uploadUrl>`
   - Header obligatorio: `Content-Type: image/jpeg`
   - Body: bytes binarios del frame JPEG.

---

## 4. Raspberry Pi Zero + Webcam USB (Kinesis Video Streams)

### A. Identidad y Credenciales Temporales
- **Thing Name / Client ID**: `smart-hotel-rpi-cam-<env>`
- **Role Alias**: `smart-hotel-rpi-kvs-alias-<env>` (entrega credenciales STS con permiso sobre `smart-hotel-entrance-cam-<env>`).
- **Certificados**: Generar X.509, adjuntar al Thing `smart-hotel-rpi-cam-<env>` y adjuntar la política `smart-hotel-rpi-policy-<env>`.
- *(Opcional MQTT)*: Puede publicar su estado en `building/<env>/camera/status` por el puerto `8883`.

### B. Streaming a KVS (`kvssink` / GStreamer)
- **Stream Name**: `smart-hotel-entrance-cam-<env>`
- **Formato**: `video/h264` (usar codificador por hardware `v4l2h264enc` o H.264 nativo de la webcam para no saturar la CPU de la Pi Zero).
- **Autenticación en `kvssink`**: Usar el modo `iot-certificate` apuntando a:
  - `endpoint`: `<iot-credential-provider-endpoint>`
  - `cert-path`, `key-path`, `ca-path` (`AmazonRootCA1.pem`)
  - `role-alias`: `smart-hotel-rpi-kvs-alias-<env>`
  - `iot-thing-name`: `smart-hotel-rpi-cam-<env>`

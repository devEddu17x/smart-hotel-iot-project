# Guía de Integración — 2. App Móvil (Android)

Referencia técnica para conectar la aplicación Android con Cognito, HTTP API v2, AWS IoT Core (MQTT over WSS) y video HLS (Media3 / ExoPlayer).

---

## 1. Valores de configuración (Outputs del Stack)

Obtener del stack `smart-hotel-iot-<env>` (`us-east-1`):
- `UserPoolId`, `UserPoolClientId`, `IdentityPoolId`
- `ApiUrl` (base URL de HTTP API v2, incluye `/<env>`)
- **IoT Data ATS Endpoint**: `aws iot describe-endpoint --endpoint-type iot:Data-ATS` (`<id>-ats.iot.us-east-1.amazonaws.com`)

---

## 2. Autenticación (Amazon Cognito)

- **Cliente**: `UserPoolClient` sin Client Secret (`ALLOW_USER_SRP_AUTH`, `ALLOW_REFRESH_TOKEN_AUTH`).
- **JWT**:
  - Usar el **ID Token** como `Authorization: Bearer <idToken>` contra HTTP API v2.
  - El claim `department` (`"201"`, `"301"`, `"401"`) viene dentro del ID Token (inyectado por `PreTokenGeneration`).
- **Identity Pool**: Intercambiar el `idToken` por credenciales AWS temporales (`AccessKeyId`, `SecretAccessKey`, `SessionToken`) para firmar la conexión MQTT over WSS.

---

## 3. Endpoints HTTP API v2

Base URL: `<ApiUrl>` — Header: `Authorization: Bearer <idToken>`

| Método | Ruta | Body / Params | Notas |
| :--- | :--- | :--- | :--- |
| `GET` | `/state` | — | Estado inicial del Device Shadow (`desired` / `reported`). |
| `POST` | `/commands/lights` | `{"state": true \| false}` | Controla luz del departamento del usuario. |
| `POST` | `/commands/garage` | `{"action": "open" \| "close" \| "stop"}` | `409 Conflict` si el motor ya está en movimiento. |
| `POST` | `/commands/door` | `{"action": "open"}` | Abre puerta principal. |
| `POST` | `/commands/alarm` | `{"action": "silence"}` | Silencia/resetea la alarma. |
| `GET` | `/camera/stream` | — | Devuelve `{"hlsUrl": "https://...m3u8"}` (válida 300s). |
| `GET` | `/camera/snapshots` | — | Lista últimos 20 snapshots S3 con URLs firmadas. |
| `GET` | `/me` | — | Perfil de `UsersTable` y `departmentId`. |
| `PUT` | `/me` | `{"name": "..."}` | Solo modifica `name` (`403` si incluye `departmentId`). |
| `GET` | `/rfid-cards` | — | Lista tarjetas RFID del departamento. |
| `POST` | `/rfid-cards` | `{"cardUid": "A1B2C3D4", "label": "..."}` | Agrega tarjeta RFID al departamento. |
| `DELETE` | `/rfid-cards/{cardUid}` | Path `cardUid` | Elimina tarjeta del departamento. |
| `GET` | `/events` | — | Últimos 30 eventos de `EventsTable`. |

---

## 4. Streaming de Sensores — MQTT over WSS

- **Endpoint y Puerto**: `wss://<iot-ats-endpoint>:443/mqtt` (autenticación **AWS SigV4** con credenciales temporales de Cognito Identity Pool).
- **Permisos del rol autenticado**: Solo `iot:Connect`, `iot:Subscribe`, `iot:Receive` (los comandos físicos siempre pasan por HTTP API v2).
- **Tópicos a suscribirse**:
  - `building/<env>/events` (alertas, intrusión, accesos RFID)
  - `building/<env>/telemetry` (estado en vivo de puerta, luces, cochera)
  - `building/<env>/camera/status` (estado de cámara Raspberry Pi)

---

## 5. Reproducción de Video y Snapshots

- **Video en vivo (KVS HLS)**:
  - Pedir `GET /camera/stream` y pasar `hlsUrl` (`.m3u8`) a **AndroidX Media3 / ExoPlayer** (`HlsMediaSource`).
  - Cuando falten pocos segundos de los `300s` de expiración (o ante error `403` de token expirado en el stream), volver a pedir `GET /camera/stream`.
- **Snapshots (ESP32-CAM)**:
  - Pedir `GET /camera/snapshots` y cargar cada `url` firmada con Coil/Glide.

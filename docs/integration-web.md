# Guía de Integración — 1. Web (Chromium SPA)

Referencia técnica para conectar el frontend Web con la infraestructura AWS ya desplegada (Cognito, HTTP API v2, IoT Core MQTT over WSS, KVS HLS y S3/CloudFront).

---

## 1. Valores de configuración (Outputs del Stack)

Obtén los valores del entorno (`dev` | `qa` | `prod`) desde los `Outputs` de CloudFormation (`smart-hotel-iot-<env>`):
- `UserPoolId`, `UserPoolClientId`, `IdentityPoolId`
- `ApiUrl` (base URL de HTTP API v2, incluye stage `/<env>`)
- `WebBucketName`, `WebUrl`
- **IoT Data ATS Endpoint**: obtener vía `aws iot describe-endpoint --endpoint-type iot:Data-ATS` (formato `<id>-ats.iot.us-east-1.amazonaws.com`).
- **Región**: `us-east-1`

---

## 2. Autenticación (Amazon Cognito)

- **Flujo**: Cognito User Pool (`ALLOW_USER_SRP_AUTH`, `ALLOW_REFRESH_TOKEN_AUTH`, sin Client Secret).
- **Tokens**:
  - Enviar el **ID Token** (`Authorization: Bearer <idToken>`) en todas las peticiones a HTTP API v2.
  - El ID Token incluye el claim `department` (`"201"`, `"301"`, `"401"`) inyectado por `PreTokenGeneration`.
- **Credenciales temporales para MQTT**: Intercambiar el `idToken` en **Cognito Identity Pool** (`IdentityPoolId`) para obtener credenciales AWS temporales (`AccessKeyId`, `SecretAccessKey`, `SessionToken`).

---

## 3. HTTP API v2 (REST / JSON)

Base URL: `<ApiUrl>` — Header obligatorio: `Authorization: Bearer <idToken>`

| Método | Ruta | Body / Params | Respuesta / Notas |
| :--- | :--- | :--- | :--- |
| `GET` | `/state` | — | Estado actual del Device Shadow (`desired` / `reported`). |
| `POST` | `/commands/lights` | `{"state": true \| false}` | Controla la luz del departamento del usuario (`department` del JWT). |
| `POST` | `/commands/garage` | `{"action": "open" \| "close" \| "stop"}` | Devuelve `409 Conflict` si el motor ya está en movimiento. |
| `POST` | `/commands/door` | `{"action": "open"}` | Abre puerta principal (servomotor). |
| `POST` | `/commands/alarm` | `{"action": "silence"}` | Silencia/resetea la alarma. |
| `GET` | `/camera/stream` | — | `{"hlsUrl": "https://...m3u8"}` (expira en 300s). |
| `GET` | `/camera/snapshots` | — | Últimos 20 snapshots S3 con Presigned URLs (`url`). |
| `GET` | `/me` | — | Perfil del usuario en `UsersTable` + `departmentId`. |
| `PUT` | `/me` | `{"name": "..."}` | Solo actualiza `name` (`403` si envía `departmentId`). |
| `GET` | `/rfid-cards` | — | Lista tarjetas del departamento del usuario. |
| `POST` | `/rfid-cards` | `{"cardUid": "A1B2C3D4", "label": "..."}` | Registra tarjeta para el departamento del usuario. |
| `DELETE` | `/rfid-cards/{cardUid}` | Path `cardUid` | Elimina tarjeta (solo si pertenece al mismo dpto). |
| `GET` | `/events` | — | Últimos 30 eventos de `EventsTable`. |

---

## 4. Tiempo Real — MQTT over WebSockets (WSS)

- **Protocolo y Puerto**: `wss://<iot-ats-endpoint>:443/mqtt` firmado con **AWS SigV4** usando las credenciales del Identity Pool.
- **Tópicos permitidos para suscripción** (`iot:Subscribe` / `iot:Receive`):
  - `building/<env>/events` — Eventos puntuales (RFID, puerta forzada, alertas).
  - `building/<env>/telemetry` — Estado en vivo (sensores, luces, cochera).
  - `building/<env>/camera/status` — Estado de la cámara en Raspberry Pi.
  - *(Wildcard válido: `building/<env>/#`)*.
- **Nota**: La Web **no** publica comandos directo por MQTT (`AuthenticatedRole` solo tiene `Connect`, `Subscribe`, `Receive`); todos los comandos se envían por HTTP API v2.

---

## 5. Video y Snapshots (Chromium)

- **Video en vivo (Raspberry Pi / KVS)**: Consumir `GET /camera/stream` y reproducir `hlsUrl` (`.m3u8`) con **`hls.js`** sobre `<video>`.
- **Snapshots (ESP32-CAM)**: Consumir `GET /camera/snapshots` y renderizar las URLs firmadas en `<img>` (el bucket S3 ya tiene CORS habilitado para `GET`).

---

## 6. Despliegue del Frontend (S3 + CloudFront OAC)

El bucket S3 es privado; solo es accesible vía CloudFront (`WebUrl`), con fallback de errores `403`/`404` $\rightarrow$ `/index.html` para rutas SPA:

```bash
aws s3 sync ./dist s3://<WebBucketName> --delete
```

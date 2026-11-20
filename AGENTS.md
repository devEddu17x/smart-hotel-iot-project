# AGENTS.md — Smart Hotel / Residential IoT Project

## 1. Contexto del Proyecto
Sistema IoT para un edificio residencial de mínimo 3 departamentos + áreas comunes (puerta principal con RFID RC522, sensor magnético, buzzer, servomotor en puerta principal, motor paso a paso en cochera, 3 circuitos independientes de iluminación, ESP32-CAM y Raspberry Pi Zero con webcam USB).
El sistema físico debe mantener su operación básica de seguridad y control local aun sin conexión a Internet (ver [docs/project.md](docs/project.md) y [firmware/firmware.ino](firmware/firmware.ino)).

## 2. Clientes Objetivo
- **Web**: SPA enfocada en navegadores basados en Chromium (reproducción de video HLS vía `hls.js`, tiempo real vía MQTT over WSS).
- **Android**: App nativa Android (reproducción de video HLS vía Media3/ExoPlayer, tiempo real vía MQTT over WSS).

## 3. Stack Definitivo en AWS
- **IaC**: AWS SAM (`Transform: AWS::Serverless-2016-10-31`) sobre CloudFormation usando **Nested Stacks**.
- **Entornos**: `dev`, `qa`, `prod` (aislados mediante `samconfig.toml` y archivos de parámetros por entorno).
- **Dispositivos $\leftrightarrow$ Nube $\rightarrow$ Web/Android (Tiempo Real)**:
  - **AWS IoT Core**: MQTT sobre TLS 1.2 (certificados X.509) para ESP32, Device Shadow para sincronización de estado (`desired`/`reported`), IoT Rules Engine hacia DynamoDB/Lambda.
  - **Web y Android**: Conexión directa por **MQTT over WebSockets (WSS)** usando credenciales temporales de **Cognito Identity Pool** para recibir telemetría y alertas en tiempo real.
- **Autenticación y Autorización**:
  - **Amazon Cognito**: User Pool (login, grupos/atributos por departamento `1`, `2`, `3` y `admin`) + Identity Pool (acceso temporal a IoT Core WSS).
- **API & Lógica de Negocio (CRUD y Comandos)**:
  - **Amazon API Gateway (HTTP API v2) + AWS Lambda**: Consultas de usuario, historial, gestión de tarjetas RFID y envío validado de comandos hacia IoT Core (verificando permisos por departamento).
- **Base de Datos**:
  - **Amazon DynamoDB**: Historial de eventos/alarmas, catálogo de tarjetas RFID autorizadas y metadatos de departamentos.
- **Video y Vigilancia**:
  - **Raspberry Pi Zero + Webcam USB**: **Amazon Kinesis Video Streams (HLS)** (`AWS::KinesisVideo::Stream`). Lambda genera URLs `.m3u8` firmadas (`GetHLSStreamingSessionURL`).
  - **ESP32-CAM**: Captura de snapshots JPEG por evento/intervalo subidos a **Amazon S3** mediante *Presigned URLs*.
- **Hosting Web**:
  - **Amazon S3 + CloudFront** con **Origin Access Control (OAC)**.

## 4. Estructura Estándar de Infraestructura (Planificada)
```text
infra/
├── template.yaml         # Root Stack (SAM/CloudFormation) que orquesta los Nested Stacks
├── samconfig.toml        # Configuraciones de despliegue por entorno ([dev], [qa], [prod])
├── params/               # Parámetros específicos por entorno
│   ├── dev.json
│   ├── qa.json
│   └── prod.json
├── stacks/               # Nested Stacks modulares por dominio
│   ├── auth.yaml         # Cognito User Pool + Identity Pool + IAM Roles
│   ├── data.yaml         # Tablas DynamoDB
│   ├── iot.yaml          # AWS IoT Core (Thing, Policies, TopicRules)
│   ├── media.yaml        # Kinesis Video Stream (HLS) + Bucket S3 Snapshots
│   ├── api.yaml          # API Gateway REST + Funciones Lambda
│   └── web.yaml          # Bucket S3 Web + CloudFront (OAC)
└── src/
    └── lambdas/          # Código fuente de las Lambdas organizado por dominio
        ├── commands/     # Control físico validado (luces, cochera, alarma)
        ├── camera/       # Presigned URLs S3 (ESP32-CAM) y URLs HLS (KVS)
        └── users/        # CRUD de usuario, tarjetas RFID e historial
```

## 5. Reglas de Trabajo y Aprendizaje
- **Enfoque incremental y educativo**: No generar toda la infraestructura ni todos los templates de golpe. Construir, explicar y validar módulo por módulo (stack por stack).
- **Modularidad**: No concentrar todos los recursos en un único `template.yaml`; usar Nested Stacks en `infra/stacks/`.
- **Parametrización por entorno**: Todo recurso debe aceptar el parámetro de entorno (`Environment`: `dev` | `qa` | `prod`) para evitar colisiones de nombres y permitir despliegues independientes.
- **Idioma en código e infraestructura**: Todos los comentarios, `Description` y mensajes dentro de templates/código deben estar en inglés, sin comentarios obvios o relleno innecesario.

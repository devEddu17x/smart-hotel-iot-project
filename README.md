# Smart Hotel — Residential IoT Project

Sistema IoT para un edificio residencial de 3 departamentos (`201`, `301`, `401`) y áreas comunes (puerta principal con RFID, sensor magnético, buzzer, cochera automatizada, iluminación por departamento y vigilancia por video).

## Estructura del Repositorio

- `firmware/` — Código del microcontrolador ESP32 DevKit (`firmware.ino`).
- `infra/` — Infraestructura en AWS con AWS SAM / CloudFormation (Nested Stacks para `dev`, `qa`, `prod`).
- `apps/web/` — Frontend Web (SPA para navegadores Chromium).
- `apps/android/` — Aplicación móvil Android.
- `docs/` — Especificación del proyecto y guías técnicas de integración.

## Arquitectura en AWS

- **IoT & Tiempo Real**: AWS IoT Core (MQTT/TLS `8883`, Device Shadow, Topic Rules, MQTT over WSS `443`).
- **Autenticación**: Amazon Cognito (User Pool + Identity Pool + `PreTokenGeneration` Lambda).
- **API & Lógica**: Amazon API Gateway (HTTP API v2) + AWS Lambda (Python 3.12).
- **Base de Datos**: Amazon DynamoDB (`UsersTable`, `RfidCardsTable`, `EventsTable`).
- **Video & Snapshots**: Amazon Kinesis Video Streams (HLS) + Amazon S3.
- **Hosting Web**: Amazon S3 + CloudFront (OAC).

## Despliegue Rápido (Infraestructura)

```bash
cd infra
sam build
sam deploy --config-env dev
```

## Documentación

- [Enunciado y requerimientos](docs/project.md)
- [Guía de despliegue con AWS SAM](docs/aws-sam-deploy-guide.md)
- [Esquema de DynamoDB](docs/dynamodb-schema.md)
- [Teoría: AWS IoT Core](docs/aws-iot-core.md)
- [Teoría: Kinesis Video Streams](docs/aws-kinesis-video-streams.md)
- [Integración Web](docs/integration-web.md)
- [Integración Android](docs/integration-android.md)
- [Integración Hardware (ESP32, ESP32-CAM, Raspberry Pi)](docs/integration-hardware.md)

import json
import os
import time
from datetime import datetime, timezone
import boto3
from botocore.exceptions import ClientError

iot_data = boto3.client("iot-data")
dynamodb = boto3.resource("dynamodb")
events_table = dynamodb.Table(os.environ["EVENTS_TABLE_NAME"])

ENVIRONMENT = os.environ["ENVIRONMENT"]
THING_NAME = os.environ["THING_NAME"]
GARAGE_BUSY_STATES = {"opening", "closing", "Abriendo... (Antihorario)", "Cerrando... (Horario)"}


def _response(status_code, body):
    return {
        "statusCode": status_code,
        "headers": {"Content-Type": "application/json"},
        "body": json.dumps(body),
    }


def _get_claims(event):
    return event.get("requestContext", {}).get("authorizer", {}).get("jwt", {}).get("claims", {})


def _get_shadow_state():
    try:
        shadow = iot_data.get_thing_shadow(thingName=THING_NAME)
        payload = json.loads(shadow["payload"].read().decode("utf-8"))
        return payload.get("state", {})
    except ClientError as exc:
        if exc.response["Error"]["Code"] == "ResourceNotFoundException":
            return {}
        raise


def _log_event(event_type, department_id, user_id, details):
    now = datetime.now(timezone.utc)
    events_table.put_item(
        Item={
            "buildingId": "main",
            "timestamp": now.strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z",
            "ttl": int(time.time()) + 2592000,
            "eventType": event_type,
            "source": "api",
            "departmentId": department_id,
            "userId": user_id,
            **details,
        }
    )


def handler(event, context):
    route_key = event.get("routeKey", "")
    claims = _get_claims(event)
    user_id = claims.get("sub", "unknown")
    department_id = claims.get("department")

    if route_key == "GET /state":
        return _response(200, _get_shadow_state())

    body = json.loads(event.get("body") or "{}")

    if route_key == "POST /commands/lights":
        if not department_id:
            return _response(403, {"error": "User has no assigned department"})
        state = body.get("state")
        if not isinstance(state, bool):
            return _response(400, {"error": "'state' must be a boolean"})

        shadow_update = {
            "state": {
                "desired": {
                    "lights": {department_id: state}
                }
            }
        }
        iot_data.update_thing_shadow(
            thingName=THING_NAME,
            payload=json.dumps(shadow_update),
        )
        _log_event("LIGHT_CMD", department_id, user_id, {"state": state})
        return _response(200, {"departmentId": department_id, "light": state})

    if route_key == "POST /commands/garage":
        action = body.get("action")
        if action not in ("open", "close", "stop"):
            return _response(400, {"error": "'action' must be 'open', 'close', or 'stop'"})

        if action in ("open", "close"):
            reported = _get_shadow_state().get("reported", {})
            garage_state = reported.get("garage", "")
            if garage_state in GARAGE_BUSY_STATES:
                return _response(409, {
                    "error": "Garage operation already in progress",
                    "currentState": garage_state,
                })

        iot_data.publish(
            topic=f"building/{ENVIRONMENT}/commands/garage",
            qos=1,
            payload=json.dumps({
                "action": action,
                "userId": user_id,
                "ts": int(time.time()),
            }),
        )
        _log_event("GARAGE_CMD", "common", user_id, {"action": action})
        return _response(200, {"garage": action})

    if route_key == "POST /commands/door":
        action = body.get("action")
        if action != "open":
            return _response(400, {"error": "'action' must be 'open'"})

        iot_data.publish(
            topic=f"building/{ENVIRONMENT}/commands/door",
            qos=1,
            payload=json.dumps({
                "action": "open",
                "userId": user_id,
                "ts": int(time.time()),
            }),
        )
        _log_event("DOOR_CMD", "common", user_id, {"action": "open"})
        return _response(200, {"door": "open"})

    if route_key == "POST /commands/alarm":
        action = body.get("action")
        if action != "silence":
            return _response(400, {"error": "'action' must be 'silence'"})

        iot_data.publish(
            topic=f"building/{ENVIRONMENT}/commands/alarm",
            qos=1,
            payload=json.dumps({
                "action": "silence",
                "userId": user_id,
                "ts": int(time.time()),
            }),
        )
        _log_event("ALARM_CMD", "common", user_id, {"action": action})
        return _response(200, {"alarm": "silenced"})

    return _response(404, {"error": "Route not found"})

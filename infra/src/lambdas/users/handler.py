import json
import os
from datetime import datetime, timezone
import boto3
from boto3.dynamodb.conditions import Key

dynamodb = boto3.resource("dynamodb")
users_table = dynamodb.Table(os.environ["USERS_TABLE_NAME"])
rfid_table = dynamodb.Table(os.environ["RFID_CARDS_TABLE_NAME"])
events_table = dynamodb.Table(os.environ["EVENTS_TABLE_NAME"])

VALID_DEPARTMENTS = {"201", "301", "401"}


def _response(status_code, body):
    return {
        "statusCode": status_code,
        "headers": {"Content-Type": "application/json"},
        "body": json.dumps(body, default=str),
    }


def _get_claims(event):
    return event.get("requestContext", {}).get("authorizer", {}).get("jwt", {}).get("claims", {})


def _resolve_department(claims, user_id):
    dept = claims.get("department")
    if dept:
        return dept
    user_item = users_table.get_item(Key={"userId": user_id}).get("Item")
    return user_item.get("departmentId") if user_item else None


def handler(event, context):
    route_key = event.get("routeKey", "")
    claims = _get_claims(event)
    user_id = claims.get("sub")
    if not user_id:
        return _response(401, {"error": "Unauthorized"})

    if route_key == "GET /me":
        item = users_table.get_item(Key={"userId": user_id}).get("Item")
        return _response(200, {
            "userId": user_id,
            "email": claims.get("email"),
            "departmentId": _resolve_department(claims, user_id),
            "profile": item,
        })

    if route_key == "PUT /me":
        body = json.loads(event.get("body") or "{}")
        if "departmentId" in body:
            return _response(403, {"error": "Users cannot self-assign or change departmentId"})

        name = str(body.get("name", "")).strip()
        if not name:
            return _response(400, {"error": "name is required"})

        existing = users_table.get_item(Key={"userId": user_id}).get("Item") or {}
        now = datetime.now(timezone.utc).isoformat()
        item = {
            **existing,
            "userId": user_id,
            "email": claims.get("email", existing.get("email", "")),
            "name": name,
            "updatedAt": now,
        }
        users_table.put_item(Item=item)
        return _response(200, item)

    if route_key == "GET /rfid-cards":
        department_id = _resolve_department(claims, user_id)
        if not department_id:
            return _response(403, {"error": "User has no assigned department"})
        resp = rfid_table.query(
            IndexName="DepartmentIndex",
            KeyConditionExpression=Key("departmentId").eq(department_id),
        )
        return _response(200, {"cards": resp.get("Items", [])})

    if route_key == "POST /rfid-cards":
        department_id = _resolve_department(claims, user_id)
        if not department_id:
            return _response(403, {"error": "User has no assigned department"})

        body = json.loads(event.get("body") or "{}")
        card_uid = str(body.get("cardUid", "")).strip().upper()
        label = str(body.get("label", "")).strip()
        if not card_uid:
            return _response(400, {"error": "cardUid is required"})

        item = {
            "cardUid": card_uid,
            "departmentId": department_id,
            "label": label,
            "active": True,
            "createdAt": datetime.now(timezone.utc).isoformat(),
        }
        rfid_table.put_item(Item=item)
        return _response(201, item)

    if route_key == "DELETE /rfid-cards/{cardUid}":
        department_id = _resolve_department(claims, user_id)
        card_uid = (event.get("pathParameters") or {}).get("cardUid", "").upper()
        existing = rfid_table.get_item(Key={"cardUid": card_uid}).get("Item")
        if not existing:
            return _response(404, {"error": "Card not found"})
        if existing.get("departmentId") != department_id:
            return _response(403, {"error": "Forbidden"})

        rfid_table.delete_item(Key={"cardUid": card_uid})
        return _response(200, {"deleted": card_uid})

    if route_key == "GET /events":
        resp = events_table.query(
            KeyConditionExpression=Key("buildingId").eq("main"),
            ScanIndexForward=False,
            Limit=30,
        )
        return _response(200, {"events": resp.get("Items", [])})

    return _response(404, {"error": "Route not found"})

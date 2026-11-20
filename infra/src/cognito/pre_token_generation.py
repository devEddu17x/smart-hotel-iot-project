import os
import boto3

dynamodb = boto3.resource("dynamodb")
users_table = dynamodb.Table(os.environ["USERS_TABLE_NAME"])


def handler(event, context):
    user_id = event["request"]["userAttributes"]["sub"]
    response = users_table.get_item(Key={"userId": user_id})
    item = response.get("Item")

    if item and "departmentId" in item:
        event["response"] = {
            "claimsOverrideDetails": {
                "claimsToAddOrOverride": {
                    "department": str(item["departmentId"])
                }
            }
        }

    return event

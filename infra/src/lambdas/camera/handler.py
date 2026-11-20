import json
import os
from datetime import datetime, timezone
import boto3

kvs = boto3.client("kinesisvideo")
s3 = boto3.client("s3")

KVS_STREAM_NAME = os.environ["KVS_STREAM_NAME"]
SNAPSHOTS_BUCKET = os.environ["SNAPSHOTS_BUCKET_NAME"]
DEVICE_API_KEY = os.environ.get("DEVICE_API_KEY", "")


def _response(status_code, body):
    return {
        "statusCode": status_code,
        "headers": {"Content-Type": "application/json"},
        "body": json.dumps(body),
    }


def handler(event, context):
    route_key = event.get("routeKey", "")

    if route_key == "GET /camera/stream":
        endpoint_resp = kvs.get_data_endpoint(
            StreamName=KVS_STREAM_NAME,
            APIName="GET_HLS_STREAMING_SESSION_URL",
        )
        kvs_archived = boto3.client(
            "kinesis-video-archived-media",
            endpoint_url=endpoint_resp["DataEndpoint"],
        )
        hls_resp = kvs_archived.get_hls_streaming_session_url(
            StreamName=KVS_STREAM_NAME,
            PlaybackMode="LIVE",
            Expires=300,
        )
        return _response(200, {"hlsUrl": hls_resp["HLSStreamingSessionURL"]})

    if route_key == "GET /camera/snapshots":
        listed = s3.list_objects_v2(
            Bucket=SNAPSHOTS_BUCKET,
            Prefix="snapshots/",
        )
        objects = sorted(
            listed.get("Contents", []),
            key=lambda obj: obj["LastModified"],
            reverse=True,
        )[:20]

        snapshots = []
        for obj in objects:
            url = s3.generate_presigned_url(
                "get_object",
                Params={"Bucket": SNAPSHOTS_BUCKET, "Key": obj["Key"]},
                ExpiresIn=300,
            )
            snapshots.append({
                "key": obj["Key"],
                "lastModified": obj["LastModified"].isoformat(),
                "url": url,
            })
        return _response(200, {"snapshots": snapshots})

    if route_key == "POST /camera/snapshots/upload-url":
        headers = {k.lower(): v for k, v in (event.get("headers") or {}).items()}
        if not DEVICE_API_KEY or headers.get("x-device-key") != DEVICE_API_KEY:
            return _response(401, {"error": "Invalid device key"})

        ts = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        key = f"snapshots/{ts}.jpg"
        upload_url = s3.generate_presigned_url(
            "put_object",
            Params={
                "Bucket": SNAPSHOTS_BUCKET,
                "Key": key,
                "ContentType": "image/jpeg",
            },
            ExpiresIn=60,
        )
        return _response(200, {"key": key, "uploadUrl": upload_url})

    return _response(404, {"error": "Route not found"})

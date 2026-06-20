# python baseline server for the comparison benchmark: a raw ASGI app, /plaintext and /json, no framework.
# json is serialized per request (same work as the other runtimes, not a cached body).
# run with uvicorn: uvicorn asgi_server:app --host 127.0.0.1 --port $PORT --workers $WORKERS
import json


async def app(scope, receive, send):
    assert scope["type"] == "http"
    if scope["path"] == "/plaintext":
        body = b"Hello, World!"
        content_type = b"text/plain"
    else:
        body = json.dumps({"hello": "world"}).encode()
        content_type = b"application/json"
    await send(
        {
            "type": "http.response.start",
            "status": 200,
            "headers": [(b"content-type", content_type)],
        }
    )
    await send({"type": "http.response.body", "body": body})

"""Bounded HTTPS API hosted behind Azure Container Apps TLS ingress."""
import os
from functools import lru_cache

from azure.data.tables import TableServiceClient
from azure.identity import DefaultAzureCredential
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse

from .protocol import MAX_BODY, Rejected
from .service import DeviceService
from .admission import Admission
from .store import TableStore


def create_azure_store():
    endpoint = os.environ["TABLE_ENDPOINT"]
    if not endpoint.startswith("https://") or not endpoint.endswith(".table.core.windows.net"):
        raise ValueError("An Azure HTTPS Table endpoint is required")
    credential = DefaultAzureCredential(managed_identity_client_id=os.environ.get("AZURE_CLIENT_ID"))
    tables = TableServiceClient(endpoint, credential=credential, logging_enable=False,
                                connection_timeout=5, read_timeout=15, retry_total=2)
    return TableStore(tables.get_table_client("DeviceState"), tables.get_table_client("DeviceRegistry"))


azure_store = lru_cache(maxsize=1)(create_azure_store)

def create_app(service=None, admission=None):
    admission = admission or Admission()
    app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None)

    @app.get("/health")
    def health():
        return {"status": "ok"}

    @app.post("/v1/device")
    async def device(request: Request):
        if not admission.accept():
            return JSONResponse({"error": "slow_down"}, 429, headers={"Retry-After": "30"})
        if request.headers.get("content-type", "").split(";")[0] != "application/json":
            return JSONResponse({"error": "json_required"}, 415)
        body = bytearray()
        async for chunk in request.stream():
            body.extend(chunk)
            if len(body) > MAX_BODY:
                return JSONResponse({"error": "request_too_large"}, 413)
        from starlette.concurrency import run_in_threadpool
        try:
            active = service or DeviceService(azure_store(), os.environ["DEVICE_SUFFIX"])
            reply = await run_in_threadpool(active.handle, bytes(body), request.headers.get("x-device-signature", request.headers.get("x-tempo-signature", "")))
            return JSONResponse(reply.body, reply.status, headers={"Cache-Control": "no-store", "Retry-After": "30"})
        except Rejected as error:
            return JSONResponse({"error": error.code}, error.status, headers={"Cache-Control": "no-store", "Retry-After": "30"})
        except Exception:
            return JSONResponse({"error": "service_unavailable"}, 503, headers={"Retry-After": "60"})

    return app


app = create_app()

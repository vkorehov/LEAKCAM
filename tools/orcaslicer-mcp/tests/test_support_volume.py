import httpx, json, respx
from orcaslicer_mcp.config import Config
from orcaslicer_mcp.client import OrcaClient

CFG = Config(base_url="http://x:13130", token="tok", timeout=5)
URL = "http://x:13130/api/v1/objects/7/support_volume"


@respx.mock
async def test_add_blocker():
    vols = [{"index": 1, "type": "blocker", "min": [165, 180, 0], "max": [180, 195, 10]}]
    route = respx.post(URL).mock(return_value=httpx.Response(200, json={"id": 7, "support_volumes": vols}))
    async with OrcaClient(CFG) as c:
        r = await c.add_support_volume(7, "blocker", (165, 180, 0), (180, 195, 10))
    assert json.loads(route.calls.last.request.read()) == {"type": "blocker", "min": [165, 180, 0], "max": [180, 195, 10]}
    assert r["support_volumes"][0]["type"] == "blocker"


@respx.mock
async def test_delete_all_and_one_kind():
    route = respx.delete(URL).mock(return_value=httpx.Response(200, json={"id": 7, "deleted": 1, "support_volumes": []}))
    async with OrcaClient(CFG) as c:
        await c.delete_support_volumes(7)
        assert "type" not in route.calls.last.request.url.params
        await c.delete_support_volumes(7, "enforcer")
        assert route.calls.last.request.url.params["type"] == "enforcer"

import httpx, json, pytest, respx
from orcaslicer_mcp.config import Config
from orcaslicer_mcp.client import OrcaClient
from orcaslicer_mcp.guard import BlockedKey

CFG = Config(base_url="http://x:13130", token="tok", timeout=5)
URL = "http://x:13130/api/v1/plate/custom_gcode"


@respx.mock
async def test_set_custom_allowed(monkeypatch):
    monkeypatch.setenv("ORCA_MCP_ALLOW_KEYS", "machine_start_gcode,layer_custom_gcode")
    items = [{"print_z": 2.0, "type": "custom", "gcode": "M104 S220"}]
    route = respx.put(URL).mock(return_value=httpx.Response(200, json={"plate": 0, "items": items}))
    async with OrcaClient(CFG) as c:
        r = await c.set_layer_gcode(items)
    assert json.loads(route.calls.last.request.read()) == {"items": items}
    assert r["items"][0]["gcode"] == "M104 S220"


@respx.mock
async def test_set_custom_blocked_without_allow(monkeypatch):
    monkeypatch.delenv("ORCA_MCP_ALLOW_KEYS", raising=False)
    route = respx.put(URL).mock(return_value=httpx.Response(200, json={}))
    async with OrcaClient(CFG) as c:
        with pytest.raises(BlockedKey):
            await c.set_layer_gcode([{"print_z": 2.0, "type": "custom", "gcode": "M104 S220"}])
    assert not route.called


@respx.mock
async def test_pause_and_clear_need_no_allow(monkeypatch):
    monkeypatch.delenv("ORCA_MCP_ALLOW_KEYS", raising=False)
    route = respx.put(URL).mock(return_value=httpx.Response(200, json={"plate": 0, "items": []}))
    async with OrcaClient(CFG) as c:
        await c.set_layer_gcode([{"print_z": 3.0, "type": "pause", "message": "insert nut"}])
        await c.set_layer_gcode([])
    assert route.call_count == 2
    assert json.loads(route.calls.last.request.read()) == {"items": []}


@respx.mock
async def test_get_layer_gcode():
    respx.get(URL).mock(return_value=httpx.Response(200, json={"plate": 0, "items": []}))
    async with OrcaClient(CFG) as c:
        assert (await c.get_layer_gcode())["items"] == []

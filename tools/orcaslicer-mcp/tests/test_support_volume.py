import httpx, json, respx
from orcaslicer_mcp.config import Config
from orcaslicer_mcp.client import OrcaClient
from orcaslicer_mcp.guard import BlockedKey

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


MOD = "http://x:13130/api/v1/objects/7/modifier"


@respx.mock
async def test_add_modifier():
    mods = [{"index": 1, "name": "lip", "config": {"fuzzy_skin": "none"}, "min": [0, 0, 0], "max": [1, 1, 1]}]
    route = respx.post(MOD).mock(return_value=httpx.Response(200, json={"id": 7, "modifiers": mods}))
    async with OrcaClient(CFG) as c:
        r = await c.add_modifier(7, r"C:\hw\prints\lip.stl", {"fuzzy_skin": "none"}, "lip")
    assert json.loads(route.calls.last.request.read()) == {"mesh_path": r"C:\hw\prints\lip.stl",
                                                           "config": {"fuzzy_skin": "none"}, "name": "lip"}
    assert r["modifiers"][0]["config"]["fuzzy_skin"] == "none"


@respx.mock
async def test_modifier_with_gcode_key_is_blocked(monkeypatch):
    monkeypatch.delenv("ORCA_MCP_ALLOW_KEYS", raising=False)
    respx.get("http://x:13130/api/v1/config").mock(return_value=httpx.Response(200, json={"config": {"machine_start_gcode": "G28"}}))
    route = respx.post(MOD).mock(return_value=httpx.Response(200, json={}))
    async with OrcaClient(CFG) as c:
        try:
            await c.add_modifier(7, "x.stl", {"machine_start_gcode": "M999"})
            raised = False
        except BlockedKey:
            raised = True
    assert raised and not route.called


@respx.mock
async def test_delete_modifiers_by_name():
    route = respx.delete(MOD).mock(return_value=httpx.Response(200, json={"id": 7, "deleted": 1, "modifiers": []}))
    async with OrcaClient(CFG) as c:
        await c.delete_modifiers(7, "lip")
    assert route.calls.last.request.url.params["name"] == "lip"

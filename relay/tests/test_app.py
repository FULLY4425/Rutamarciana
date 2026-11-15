import json
import pytest
from pipeline.scenario import metadata, scenario

from relay.app import create_app


def test_health_and_routes(tmp_path):
    (tmp_path / "routes.json").write_text(json.dumps({"schema_version": 3, "dem_validity": "nodata-preserved-v1", "budget_model": "LUNAR-ANALOG-2026", "scenario": metadata(scenario()), "cell_m": 4, "cell_x_m": 4, "safe": {"time_s": 1}}))
    app = create_app(str(tmp_path))
    c = app.test_client()
    assert c.get("/health").json["ok"] is True
    assert c.get("/api/routes").json["safe"]["time_s"] == 1


def test_manifest_slim_contract_and_full_file(tmp_path):
    manifest = {"site": "jez_dtm_1m", "budget_s": 14400, "science_s": 1200, "science_stops": 1,
                "waypoints": [{"id": 1, "name": "START", "row": 2, "col": 2},
                              {"id": 2, "name": "S1", "row": 3, "col": 4}, {"id": 3, "name": "", "row": 0, "col": 0}],
                "routes": {"safe": {"time_s": 5, "distance_m": 6, "points": [[2, 2]]},
                           "fast": {"time_s": 4, "distance_m": 6, "points": [[2, 2]]},
                           "other": {"time_s": 1}},
                "layers": {"huge": [0] * 100}, "extra": {"x": True}}
    (tmp_path / "routes.json").write_text(json.dumps(manifest), encoding="utf-8")
    client = create_app(str(tmp_path)).test_client()
    slim = client.get("/api/manifest")
    assert slim.status_code == 200
    assert set(slim.json) == {"site", "waypoints", "routes", "budget_s", "science_s", "science_stops"}
    assert [w["name"] for w in slim.json["waypoints"]] == ["START", "S1"]
    assert set(slim.json["routes"]) == {"safe", "fast"}
    assert slim.json["site"] == "jez_dtm_1m" and slim.json["science_stops"] == 1
    full = client.get("/api/manifest?full=1")
    assert full.status_code == 200
    assert full.json == manifest


def test_manifest_missing_returns_404(tmp_path):
    response = create_app(str(tmp_path)).test_client().get("/api/manifest")
    assert response.status_code == 404


def test_bitacora_persists_and_replaces_full_json(tmp_path):
    c = create_app(str(tmp_path)).test_client()
    walk = {"id": 12, "fecha": "2026-11-14", "entradas": [{"hora": "12:00", "parada": "inicio"}], "extra": "á"}
    assert c.post("/api/bitacora", json=walk).status_code == 200
    path = tmp_path / "bitacora" / "12.json"
    assert json.loads(path.read_text(encoding="utf-8")) == walk
    walk["entradas"].append({"parada": "fin"})
    assert c.post("/api/bitacora", json=walk).status_code == 200
    assert json.loads(path.read_text(encoding="utf-8")) == walk
    assert list(path.parent.iterdir()) == [path]


@pytest.mark.parametrize("ident", ["../escape", "a/b", "", ".", "-1", -1, True, 1.5, None, "１２"])
def test_bitacora_rejects_unsafe_ids(tmp_path, ident):
    c = create_app(str(tmp_path)).test_client()
    response = c.post("/api/bitacora", json={"id": ident, "fecha": "2026-11-14", "entradas": []})
    assert response.status_code == 400
    assert not (tmp_path / "bitacora").exists()


@pytest.mark.parametrize("body", [[], None, {}, {"id": 1, "fecha": 4, "entradas": []}, {"id": 1, "fecha": "x", "entradas": {}}])
def test_bitacora_rejects_invalid_body(tmp_path, body):
    c = create_app(str(tmp_path)).test_client()
    assert c.post("/api/bitacora", json=body).status_code == 400
    assert not (tmp_path / "bitacora").exists()

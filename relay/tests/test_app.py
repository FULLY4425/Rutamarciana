import json

import pytest

from relay.app import create_app


def test_health_and_routes(tmp_path):
    (tmp_path / "routes.json").write_text(json.dumps({"schema_version": 3, "dem_validity": "nodata-preserved-v1", "cell_m": 4, "cell_x_m": 4, "safe": {"time_s": 1}}))
    app = create_app(str(tmp_path))
    c = app.test_client()
    assert c.get("/health").json["ok"] is True
    assert c.get("/api/routes").json["safe"]["time_s"] == 1


def test_manifest_construye_contrato(tmp_path):
    manifest = {"site": "jez_dtm_1m", "waypoints": [{"id": 1, "name": "START", "row": 2, "col": 2},
               {"id": 2, "name": "END", "row": 497, "col": 497}, {"id": 3, "row": 4, "col": 4}],
               "routes": {"safe": {"time_s": 1, "distance_m": 2, "points": [[0, 0]]},
                          "fast": {"time_s": 1}, "otra": {"time_s": 9}},
               "budget_s": 14400, "science_s": 600, "science_stops": 5, "layers": {"x": 1}}
    (tmp_path / "routes.json").write_text(json.dumps(manifest), encoding="utf-8")
    out = create_app(str(tmp_path)).test_client().get("/api/manifest")
    assert out.status_code == 200
    assert out.json["site"] == "jez_dtm_1m"
    assert out.json["waypoints"] == [{"id": 1, "name": "START", "row": 2, "col": 2}, {"id": 2, "name": "END", "row": 497, "col": 497}]
    assert out.json["routes"] == {"safe": {"time_s": 1, "distance_m": 2, "points": [[0, 0]]}, "fast": {"time_s": 1, "distance_m": None, "points": None}}
    assert out.json["budget_s"] == 14400 and out.json["science_s"] == 600 and out.json["science_stops"] == 5
    assert "layers" not in out.json


def test_manifest_404_sin_routes(tmp_path):
    assert create_app(str(tmp_path)).test_client().get("/api/manifest").status_code == 404


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

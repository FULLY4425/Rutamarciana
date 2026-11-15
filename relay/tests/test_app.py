import json

from relay.app import create_app


def test_health_and_routes(tmp_path):
    (tmp_path / "routes.json").write_text(json.dumps({"schema_version": 3, "cell_m": 4, "cell_x_m": 4, "safe": {"time_s": 1}}))
    app = create_app(str(tmp_path))
    c = app.test_client()
    assert c.get("/health").json["ok"] is True
    assert c.get("/api/routes").json["safe"]["time_s"] == 1

"""End-to-end tests against the FastAPI app via TestClient. The primary
provider is forced to "mock" (see conftest.py) so these never touch the
network or depend on Ollama being installed.
"""

from fastapi.testclient import TestClient

from app.main import app

client = TestClient(app)


def test_health_reports_mock_available_and_ollama_status():
    response = client.get("/health")
    assert response.status_code == 200
    body = response.json()
    assert body["status"] in ("ok", "degraded")
    provider_names = {p["name"] for p in body["providers"]}
    assert "mock" in provider_names
    assert "ollama" in provider_names
    mock_status = next(p for p in body["providers"] if p["name"] == "mock")
    assert mock_status["available"] is True


def test_generate_endpoint_returns_a_full_preset():
    response = client.post("/generate", json={"prompt": "Give me a Cliffs of Dover style lead tone."})
    assert response.status_code == 200
    body = response.json()
    assert body["name"]
    assert len(body["chain"]) > 0
    assert body["aiProvenance"]["disclaimer"]


def test_generate_endpoint_rejects_empty_prompt():
    response = client.post("/generate", json={"prompt": ""})
    assert response.status_code == 422


def test_modify_endpoint_returns_a_modified_preset():
    generated = client.post("/generate", json={"prompt": "clean jazz tone"}).json()
    response = client.post(
        "/modify",
        json={"preset": generated, "instruction": "add more gain and reverb"},
    )
    assert response.status_code == 200
    body = response.json()
    assert body["aiProvenance"]["matchType"] == "modified"


def test_explain_endpoint_returns_text():
    generated = client.post("/generate", json={"prompt": "bright lead tone"}).json()
    response = client.post("/explain", json={"preset": generated})
    assert response.status_code == 200
    assert len(response.json()["explanation"]) > 0


def test_song_reference_endpoint_curated_match():
    response = client.post("/song-reference", json={"query": "Cliffs of Dover"})
    assert response.status_code == 200
    body = response.json()
    assert body["aiProvenance"]["matchType"] == "song_curated"
    assert "inspired" in body["aiProvenance"]["disclaimer"].lower() or "approximation" in body["aiProvenance"]["disclaimer"].lower()


def test_song_reference_endpoint_inferred_fallback():
    response = client.post("/song-reference", json={"query": "a totally made up untracked song xyzzy"})
    assert response.status_code == 200
    body = response.json()
    assert body["aiProvenance"]["matchType"] == "song_inferred"
    assert body["aiProvenance"]["confidence"] < 0.7

from __future__ import annotations

from fastapi import APIRouter, Depends

from app.deps import get_mock_provider, get_ollama_provider
from app.providers.mock_provider import MockProvider
from app.providers.ollama_provider import OllamaProvider
from app.schemas.requests import HealthResponse, ProviderStatus

router = APIRouter(tags=["health"])


@router.get("/health", response_model=HealthResponse)
async def health(
    mock: MockProvider = Depends(get_mock_provider),
    ollama: OllamaProvider = Depends(get_ollama_provider),
) -> HealthResponse:
    ollama_available = await ollama.is_available()
    providers = [
        ProviderStatus(name=mock.name, available=await mock.is_available()),
        ProviderStatus(
            name=ollama.name,
            available=ollama_available,
            detail="" if ollama_available else "Ollama not reachable - generate/modify/explain will fall back to the mock provider.",
        ),
    ]
    status = "ok" if ollama_available else "degraded"
    return HealthResponse(status=status, providers=providers)

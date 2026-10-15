from __future__ import annotations

import logging

from fastapi import APIRouter, Depends

from app.deps import get_fallback_provider, get_primary_provider
from app.providers.base import AIProvider, ProviderUnavailableError
from app.schemas.requests import ExplainRequest, ExplainResponse

logger = logging.getLogger(__name__)
router = APIRouter(tags=["explain"])


@router.post("/explain", response_model=ExplainResponse)
async def explain_tone(
    request: ExplainRequest,
    primary: AIProvider = Depends(get_primary_provider),
    fallback: AIProvider = Depends(get_fallback_provider),
) -> ExplainResponse:
    try:
        text = await primary.explain(request.preset)
    except ProviderUnavailableError as exc:
        logger.warning("Primary provider %s unavailable, falling back to mock: %s", primary.name, exc)
        text = await fallback.explain(request.preset)
    return ExplainResponse(explanation=text)

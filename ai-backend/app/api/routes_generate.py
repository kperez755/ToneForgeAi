from __future__ import annotations

import logging

from fastapi import APIRouter, Depends

from app.deps import get_fallback_provider, get_primary_provider
from app.providers.base import AIProvider, ProviderUnavailableError
from app.schemas.preset import PresetDTO
from app.schemas.requests import GenerateRequest, ModifyRequest

logger = logging.getLogger(__name__)
router = APIRouter(tags=["generate"])


@router.post("/generate", response_model=PresetDTO)
async def generate_tone(
    request: GenerateRequest,
    primary: AIProvider = Depends(get_primary_provider),
    fallback: AIProvider = Depends(get_fallback_provider),
) -> PresetDTO:
    try:
        return await primary.generate(request.prompt)
    except ProviderUnavailableError as exc:
        logger.warning("Primary provider %s unavailable, falling back to mock: %s", primary.name, exc)
        return await fallback.generate(request.prompt)


@router.post("/modify", response_model=PresetDTO)
async def modify_tone(
    request: ModifyRequest,
    primary: AIProvider = Depends(get_primary_provider),
    fallback: AIProvider = Depends(get_fallback_provider),
) -> PresetDTO:
    try:
        return await primary.modify(request.preset, request.instruction)
    except ProviderUnavailableError as exc:
        logger.warning("Primary provider %s unavailable, falling back to mock: %s", primary.name, exc)
        return await fallback.modify(request.preset, request.instruction)

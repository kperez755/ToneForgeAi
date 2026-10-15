from __future__ import annotations

from typing import Literal

from pydantic import BaseModel, Field

from app.schemas.preset import PresetDTO


class GenerateRequest(BaseModel):
    prompt: str = Field(min_length=1, max_length=2000)


class ModifyRequest(BaseModel):
    preset: PresetDTO
    instruction: str = Field(min_length=1, max_length=2000)


class ExplainRequest(BaseModel):
    preset: PresetDTO


class ExplainResponse(BaseModel):
    explanation: str


class SongReferenceRequest(BaseModel):
    query: str = Field(
        min_length=1,
        max_length=200,
        description="Song title, optionally with artist, e.g. 'Cliffs of Dover' or 'SRV - Texas Flood'",
    )


class ProviderStatus(BaseModel):
    name: str
    available: bool
    detail: str = ""


class HealthResponse(BaseModel):
    status: Literal["ok", "degraded"]
    providers: list[ProviderStatus]

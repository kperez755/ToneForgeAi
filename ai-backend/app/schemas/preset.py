"""Pydantic models for the preset wire format.

These mirror tfcore::Preset / tfcore::ModuleState (core/include/tfcore/preset/)
field-for-field on purpose: the JSON this service emits from /generate,
/modify, and /song-reference is deserialized directly by
tfcore::PresetSerializer::fromJson on the C++ side. If a field is renamed or
retyped here, the corresponding change must be made in
core/src/preset/PresetSerializer.cpp in the same change, or the plugin will
silently drop AI-generated preset data. There is no shared schema-generation
step yet (a JSON Schema exported from here and validated against on the C++
side is a natural M9 hardening item - see docs/ROADMAP.md).
"""

from __future__ import annotations

from typing import Literal

from pydantic import BaseModel, Field, field_validator

MatchType = Literal["generated", "modified", "song_curated", "song_inferred"]


class AiProvenanceDTO(BaseModel):
    """Attached to any preset touched by AI. `disclaimer` is required and
    validated non-empty specifically so a preset can never be presented to
    the user as an exact recreation - see docs/ARCHITECTURE.md 5.2.
    """

    provider: str
    prompt: str
    match_type: MatchType = Field(alias="matchType")
    confidence: float = Field(ge=0.0, le=1.0)
    disclaimer: str

    @field_validator("disclaimer")
    @classmethod
    def disclaimer_must_not_be_empty(cls, value: str) -> str:
        if not value.strip():
            raise ValueError("disclaimer must not be empty")
        return value

    model_config = {"populate_by_name": True}


class ModuleStateDTO(BaseModel):
    module_id: str = Field(alias="moduleId")
    bypassed: bool = False
    parameters: dict[str, float] = Field(default_factory=dict)
    resource_ref: str | None = Field(default=None, alias="resourceRef")

    model_config = {"populate_by_name": True}


class PresetDTO(BaseModel):
    id: str
    name: str
    description: str = ""
    author: str = "ToneForge AI"
    schema_version: int = Field(default=1, alias="schemaVersion")
    tags: list[str] = Field(default_factory=list)
    favorite: bool = False
    chain: list[ModuleStateDTO] = Field(default_factory=list)
    ai_provenance: AiProvenanceDTO | None = Field(default=None, alias="aiProvenance")

    model_config = {"populate_by_name": True}

import pytest
from pydantic import ValidationError

from app.schemas.preset import AiProvenanceDTO, ModuleStateDTO, PresetDTO


def test_module_state_round_trips_camel_case_aliases():
    module = ModuleStateDTO(moduleId="drive.v1", bypassed=False, parameters={"gain_db": 12.0}, resourceRef="amp_x")
    dumped = module.model_dump(by_alias=True)
    assert dumped["moduleId"] == "drive.v1"
    assert dumped["resourceRef"] == "amp_x"

    restored = ModuleStateDTO.model_validate(dumped)
    assert restored.module_id == "drive.v1"
    assert restored.resource_ref == "amp_x"


def test_preset_dto_defaults():
    preset = PresetDTO(id="p1", name="Test Preset")
    assert preset.schema_version == 1
    assert preset.chain == []
    assert preset.ai_provenance is None


def test_ai_provenance_rejects_empty_disclaimer():
    with pytest.raises(ValidationError):
        AiProvenanceDTO(
            provider="mock",
            prompt="test",
            matchType="generated",
            confidence=1.0,
            disclaimer="   ",
        )


def test_ai_provenance_confidence_must_be_in_unit_range():
    with pytest.raises(ValidationError):
        AiProvenanceDTO(
            provider="mock",
            prompt="test",
            matchType="generated",
            confidence=1.5,
            disclaimer="Inspired by - approximation.",
        )


def test_preset_dto_json_uses_camel_case_when_dumped_by_alias():
    preset = PresetDTO(
        id="p1",
        name="Test",
        aiProvenance=AiProvenanceDTO(
            provider="mock", prompt="x", matchType="generated", confidence=1.0, disclaimer="Inspired by."
        ),
    )
    dumped = preset.model_dump(by_alias=True)
    assert "aiProvenance" in dumped
    assert "schemaVersion" in dumped

"""Battle story contracts: unreal/PA250Sim/data/battle_stories.json against its schema.

The Unreal campaign reads the file (UI/PA250BattleStory) and the PA250Battle commandlet checks the
matching; here the file must obey the schema, credit every picture, name towns that exist, and
leave no town of the scenario without a story or a terrain fallback. Decisions name pictures from
the same list. No service code is involved.
"""

import json
from pathlib import Path
from typing import Any

import yaml  # type: ignore[import-untyped]
from jsonschema import Draft7Validator  # type: ignore[import-untyped]

ROOT = Path(__file__).resolve().parents[4]
DATA_DIR = ROOT / "unreal" / "PA250Sim" / "data"
SCHEMA: dict[str, Any] = yaml.safe_load(
    (ROOT / "protocol" / "schemas" / "battle_stories.yaml").read_text()
)
STORIES: dict[str, Any] = json.loads((DATA_DIR / "battle_stories.json").read_text())
SCENARIO: dict[str, Any] = json.loads((DATA_DIR / "boston1775.json").read_text())
DECISIONS: list[dict[str, Any]] = json.loads(
    (DATA_DIR / "decisions_boston_1775.json").read_text()
)["decisions"]
IMAGES = {i["key"]: i for i in STORIES["images"]}


def test_stories_obey_the_schema() -> None:
    Draft7Validator.check_schema(SCHEMA)
    errors = [e.message for e in Draft7Validator(SCHEMA).iter_errors(STORIES)]
    assert not errors, errors


def test_every_story_has_a_credited_public_domain_picture() -> None:
    for story in STORIES["stories"] + STORIES["fallbacks"]:
        image = IMAGES[story["image"]]
        assert image["licence"] == "public domain"
        assert image["artist"] and image["year"] and image["collection"], story["key"]
        assert 3 <= len(story["text"]) <= 4, story["key"]
        assert story["basis"] in {"documented", "invented"}
    keys = [s["key"] for s in STORIES["stories"] + STORIES["fallbacks"]]
    assert len(keys) == len(set(keys))
    narrations = [s["narration"] for s in STORIES["stories"] + STORIES["fallbacks"]]
    assert len(narrations) == len(set(narrations))


def test_no_picture_is_shipped_that_is_only_wanted() -> None:
    stories_dir = ROOT / "unreal" / "PlayAmerica250" / "Content" / "PA250" / "Stories"
    for key, image in IMAGES.items():
        asset = stories_dir / f"T_Story_{key}.uasset"
        assert asset.exists() == (image["status"] == "imported"), key


def test_every_town_has_a_story_or_a_terrain_fallback() -> None:
    towns = {p["id"]: p for p in SCENARIO["provinces"]}
    by_town = {
        s["match"]["province"] for s in STORIES["stories"] if "province" in s["match"]
    }
    assert by_town <= set(towns)
    fallback_terrains = {f["terrain"] for f in STORIES["fallbacks"]}
    all_terrain = {t for p in towns.values() for t in p["terrain"]}
    assert fallback_terrains == all_terrain, "one generic fallback for each terrain tag"
    for town_id, town in towns.items():
        assert town_id in by_town or fallback_terrains & set(town["terrain"]), town_id


def test_decisions_name_pictures_from_the_same_list() -> None:
    for decision in DECISIONS:
        assert decision["painting"] in IMAGES, decision["id"]

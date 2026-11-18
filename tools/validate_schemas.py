#!/usr/bin/env python3
"""Validate the example files against the Rollcraft JSON schemas.

Files in schemas/examples/valid must validate, files in schemas/examples/invalid
must be rejected. The schema is chosen from the file name prefix
(preset-, bank-, velocity-curve-).

Requires: jsonschema >= 4.18   (pip install "jsonschema>=4.18")
Usage:    python tools/validate_schemas.py
"""
import json
import sys
from pathlib import Path

from jsonschema import Draft202012Validator
from referencing import Registry, Resource

ROOT = Path(__file__).resolve().parent.parent
SCHEMAS = ROOT / "schemas"
EXAMPLES = SCHEMAS / "examples"

PREFIXES = {
    "preset-": "preset.schema.json",
    "bank-": "bank.schema.json",
    "velocity-curve-": "velocity-curve.schema.json",
}


def load(path: Path):
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def build_validators():
    schemas = {p.name: load(p) for p in SCHEMAS.glob("*.schema.json")}
    registry = Registry()
    for name, schema in schemas.items():
        Draft202012Validator.check_schema(schema)
        resource = Resource.from_contents(schema)
        # Register under the $id and under the relative file name used by $ref.
        registry = registry.with_resource(schema["$id"], resource)
        registry = registry.with_resource(name, resource)
    return {
        name: Draft202012Validator(schema, registry=registry)
        for name, schema in schemas.items()
    }


def schema_for(path: Path) -> str:
    for prefix, schema_name in PREFIXES.items():
        if path.name.startswith(prefix):
            return schema_name
    raise SystemExit(f"No schema mapping for example file: {path.name}")


def main() -> int:
    validators = build_validators()
    failures = 0
    for kind, expect_valid in (("valid", True), ("invalid", False)):
        for path in sorted((EXAMPLES / kind).glob("*.json")):
            errors = list(validators[schema_for(path)].iter_errors(load(path)))
            ok = (not errors) == expect_valid
            status = "ok  " if ok else "FAIL"
            detail = ""
            if not ok:
                failures += 1
                detail = "  -> " + (errors[0].message if errors else "unexpectedly valid")
            elif errors:
                detail = "  (rejected: " + errors[0].message[:70] + ")"
            print(f"{status} {kind}/{path.name}{detail}")
    print(f"\n{failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

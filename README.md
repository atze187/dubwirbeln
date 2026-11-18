# dubwirbeln

Monophonic drum-roll instrument (VST3). LOAD. PAINT. TRIGGER. AGPLv3.

Spec: [docs/SPEC.md](docs/SPEC.md).

```powershell
./build.ps1 all        # configure, build, test
./build.ps1 validate   # Steinberg VST3 validator + pluginval (strictness 5)
python tools/validate_schemas.py
```

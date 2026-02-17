# Blueprint to AngelScript Skill

Converts Unreal Engine Blueprints to AngelScript classes using the BlueprintExporter plugin.

## How It Works

1. The **BlueprintExporter** Unreal plugin runs an HTTP server (port 7233) inside the editor
2. When invoked, Claude uses `curl` to export blueprint data from the plugin's HTTP endpoints
3. Claude analyzes the exported JSON and generates AngelScript code

## Setup

The BlueprintExporter plugin lives in `Plugins/BlueprintExporter/` and is included in the project. It loads automatically when you open the project in Unreal Editor — no manual steps needed.

To verify it's running, check the Output Log for:
```
BlueprintExporter: HTTP server started on port 7233
```

If you need to rebuild the plugin (e.g. after C++ changes), recompile via the editor or regenerate project files with `GenerateProjectFiles.bat` and build from Visual Studio.

## Usage

```
/blueprint-to-angelscript /Game/Path/To/BP_ClassName
```

Unreal Editor must be open with the project loaded.

## HTTP Endpoints

All endpoints on `http://localhost:7233`. Responses return `output_path` to a JSON file that must be read separately.

| Endpoint | Description |
|---|---|
| `GET /ping` | Check if editor is running |
| `GET /export?path=/Game/...` | Export blueprint to JSON file |
| `GET /list?filter=...` | List blueprints matching filter |
| `GET /export-struct?path=/Game/...` | Export UserDefinedStruct to JSON file |
| `GET /export-enum?path=/Game/...` | Export UserDefinedEnum to JSON file |

## Files

- **SKILL.md** - Skill definition with conversion rules and AngelScript patterns
- **README.md** - This file

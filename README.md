# Blueprint to AngelScript Skill

Converts Unreal Engine Blueprints to AngelScript classes using the BlueprintExporter plugin via MCP.

## How It Works

1. The **BlueprintExporter** Unreal plugin runs an HTTP server (port 7233) inside the editor
2. The **MCP server** (`mcp/server.mjs`) bridges Claude Code to that HTTP server as MCP tools
3. When invoked, Claude calls MCP tools to export blueprint data and generates AngelScript code

## Usage

```
/blueprint-to-angelscript /Game/Path/To/BP_ClassName
```

Unreal Editor must be open with the project loaded.

## Requirements

- Unreal Editor running with the BlueprintExporter plugin active (auto-starts on launch)
- Node.js installed (for the MCP server)
- MCP server registered in `.claude/settings.local.json` (already configured)

## MCP Tools

| Tool | Description |
|---|---|
| `ping_editor` | Check if Unreal Editor is running |
| `export_blueprint` | Export blueprint graph data (returns JSON directly) |
| `list_blueprints` | List blueprints matching optional filter |
| `export_struct` | Export UserDefinedStruct field definitions |
| `export_enum` | Export UserDefinedEnum values |

## Files

- **SKILL.md** - Skill definition with conversion rules and AngelScript patterns
- **README.md** - This file
- **mcp/server.mjs** - MCP server that bridges Claude Code to the Unreal plugin
- **mcp/package.json** - Node.js dependencies (MCP SDK)

# Blueprint to AngelScript Skill

Converts Unreal Engine Blueprints to AngelScript classes using the BlueprintExporter plugin.

## How It Works

1. The **BlueprintExporter** Unreal plugin runs an HTTP server (port 7233) inside the editor
2. The **MCP server** (`mcp/server.mjs`) bridges Claude Code to that HTTP server as MCP tools
3. When invoked, Claude calls MCP tools to export blueprint data and generates AngelScript code
4. If MCP is unavailable, Claude falls back to `curl` against the HTTP endpoints directly

## Setup

### BlueprintExporter Plugin (Unreal Editor)

The plugin lives in `Plugins/BlueprintExporter/` and is included in the project. It loads automatically when you open the project in Unreal Editor — no manual steps needed.

To verify it's running, check the Output Log for:
```
BlueprintExporter: HTTP server started on port 7233
```

If you need to rebuild the plugin (e.g. after C++ changes), recompile via the editor or regenerate project files with `GenerateProjectFiles.bat` and build from Visual Studio.

### MCP Server (first time only)

1. Run `mcp/install.bat` to install Node.js dependencies
2. Add the MCP server to your Claude Code settings (`~/.claude/settings.json`):
   ```json
   {
     "mcpServers": {
       "blueprint-exporter": {
         "command": "node",
         "args": ["<full-path-to>/mcp/server.mjs"]
       }
     }
   }
   ```
3. Restart Claude Code to pick up the new MCP server
4. Verify with `/mcp` command — should show `blueprint-exporter` as connected

### Without MCP

No setup needed — the skill falls back to `curl` against `localhost:7233` automatically. Just have Unreal Editor running.

## Usage

```
/blueprint-to-angelscript /Game/Path/To/BP_ClassName
```

Unreal Editor must be open with the project loaded.

## MCP Tools

| Tool | Description |
|---|---|
| `ping_editor` | Check if Unreal Editor is running |
| `export_blueprint` | Export blueprint graph data (returns JSON directly) |
| `list_blueprints` | List blueprints matching optional filter |
| `export_struct` | Export UserDefinedStruct field definitions |
| `export_enum` | Export UserDefinedEnum values |

## HTTP Endpoints (curl fallback)

| Endpoint | Description |
|---|---|
| `GET /ping` | Check if editor is running |
| `GET /export?path=/Game/...` | Export blueprint to JSON file |
| `GET /list?filter=...` | List blueprints matching filter |
| `GET /export-struct?path=/Game/...` | Export UserDefinedStruct to JSON file |
| `GET /export-enum?path=/Game/...` | Export UserDefinedEnum to JSON file |

All endpoints on `http://localhost:7233`. Curl endpoints return `output_path` to a JSON file that must be read separately. MCP tools return data directly.

## Files

- **SKILL.md** - Skill definition with conversion rules and AngelScript patterns
- **README.md** - This file
- **mcp/server.mjs** - MCP server that bridges Claude Code to the Unreal plugin
- **mcp/package.json** - Node.js dependencies (MCP SDK)
- **mcp/install.bat** - Run once to install Node.js dependencies
- **mcp/.gitignore** - Excludes node_modules and package-lock.json

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { readFile } from "fs/promises";

const BASE_URL = "http://localhost:7233";

async function fetchJson(url) {
  const res = await fetch(url);
  return res.json();
}

async function readOutputFile(outputPath) {
  const content = await readFile(outputPath, "utf-8");
  return JSON.parse(content);
}

const server = new McpServer({
  name: "blueprint-exporter",
  version: "1.0.0",
});

server.tool("ping_editor", "Check if Unreal Editor is running with the BlueprintExporter plugin", {}, async () => {
  try {
    const data = await fetchJson(`${BASE_URL}/ping`);
    return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
  } catch {
    return {
      content: [{ type: "text", text: "Unreal Editor is not running or BlueprintExporter plugin is not active." }],
      isError: true,
    };
  }
});

server.tool(
  "export_blueprint",
  "Export a blueprint's complete graph data (variables, components, functions, events) to JSON. Returns the full graph data directly.",
  { path: { type: "string", description: "Asset path, e.g. /Game/Core/Inventory/BP_InventoryVisual" } },
  async ({ path }) => {
    try {
      const res = await fetchJson(`${BASE_URL}/export?path=${encodeURIComponent(path)}`);
      if (!res.success) {
        return { content: [{ type: "text", text: `Export failed: ${res.error}` }], isError: true };
      }
      const data = await readOutputFile(res.output_path);
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    } catch (e) {
      return { content: [{ type: "text", text: `Error: ${e.message}` }], isError: true };
    }
  }
);

server.tool(
  "list_blueprints",
  "List available blueprints in the project, optionally filtered by name",
  { filter: { type: "string", description: "Optional filter string to match blueprint names" } },
  async ({ filter }) => {
    try {
      const url = filter ? `${BASE_URL}/list?filter=${encodeURIComponent(filter)}` : `${BASE_URL}/list`;
      const data = await fetchJson(url);
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    } catch (e) {
      return { content: [{ type: "text", text: `Error: ${e.message}` }], isError: true };
    }
  }
);

server.tool(
  "export_struct",
  "Export a UserDefinedStruct's field definitions (names, types, defaults) to JSON",
  { path: { type: "string", description: "Asset path, e.g. /Game/Data/Structs/S_MyStruct" } },
  async ({ path }) => {
    try {
      const res = await fetchJson(`${BASE_URL}/export-struct?path=${encodeURIComponent(path)}`);
      if (!res.success) {
        return { content: [{ type: "text", text: `Export failed: ${res.error}` }], isError: true };
      }
      const data = await readOutputFile(res.output_path);
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    } catch (e) {
      return { content: [{ type: "text", text: `Error: ${e.message}` }], isError: true };
    }
  }
);

server.tool(
  "export_enum",
  "Export a UserDefinedEnum's values (names, display names, numeric values) to JSON",
  { path: { type: "string", description: "Asset path, e.g. /Game/Data/Enums/E_MyEnum" } },
  async ({ path }) => {
    try {
      const res = await fetchJson(`${BASE_URL}/export-enum?path=${encodeURIComponent(path)}`);
      if (!res.success) {
        return { content: [{ type: "text", text: `Export failed: ${res.error}` }], isError: true };
      }
      const data = await readOutputFile(res.output_path);
      return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
    } catch (e) {
      return { content: [{ type: "text", text: `Error: ${e.message}` }], isError: true };
    }
  }
);

const transport = new StdioServerTransport();
await server.connect(transport);

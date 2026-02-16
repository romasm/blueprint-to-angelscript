---
name: blueprint-to-angelscript
description: Convert an Unreal Engine Blueprint to AngelScript implementation. Use when the user wants to implement a blueprint in AngelScript or sync blueprint changes to script.
argument-hint: blueprint-path
---

# Blueprint to AngelScript Converter

Convert the Blueprint at **$ARGUMENTS** to AngelScript implementation.

## Process:

### 1. Extract Blueprint Data

The BlueprintExporter plugin runs an HTTP server on port 7233 inside the running Unreal Editor. Access it via MCP tools (preferred) or curl fallback.

#### Method A: MCP Tools (preferred)

**Check if the editor is running:**
Use the `mcp__blueprint-exporter__ping_editor` tool.

**Export the blueprint:**
Use the `mcp__blueprint-exporter__export_blueprint` tool with `path` = `$ARGUMENTS`.
This returns the complete graph data directly as JSON — no need to read a separate file.

**To list available blueprints (optional):**
Use the `mcp__blueprint-exporter__list_blueprints` tool with `filter` parameter.

#### Method B: curl Fallback (if MCP tools are unavailable)

**Check if the editor is running:**
```bash
curl -s http://localhost:7233/ping
```

**Export the blueprint:**
```bash
curl -s "http://localhost:7233/export?path=/Game/$ARGUMENTS"
```
This returns `{"success": true, "output_path": "...", "file_size": ...}`. Then read the JSON file at `output_path`.

**To list available blueprints (optional):**
```bash
curl -s "http://localhost:7233/list?filter=BP_ClassName"
```

If both methods fail, ask the user to open Unreal Editor with the SignalsGame project.

### 2. Analyze the Exported Graph Data

The JSON returned by `export_blueprint` contains:
- **class_defaults**: Class-level default values that differ from parent class
  - Only includes properties modified from parent class defaults
  - Each default includes `name`, `type`, `value`, and reference metadata
  - These should be placed at the very top of the class using `default` statements
- **variables**: All blueprint variables with types, flags, and CDO defaults
  - `cdo_default_value`: Actual default value from the blueprint's Class Default Object
  - `blueprint_reference`: Clean blueprint name if the value is a blueprint reference
  - `is_blueprint_reference`: Boolean flag indicating blueprint references
  - `struct_source`: Source of struct types ("blueprint", "cpp", or "custom")
  - `is_struct_reference`: Boolean flag indicating struct properties
  - `struct_reference`: Clean struct name for blueprint structs
  - `is_enum`: Boolean flag indicating enum properties
  - `enum_name`: Name of the enum type
  - `enum_source`: Source of enum ("blueprint" or "cpp")
  - `enum_path`: Asset path for blueprint enums
- **components**: Component hierarchy with attachment parents and modified properties
  - `properties`: Array of component properties that differ from class defaults
  - Each property includes `name`, `type`, `value`, and blueprint/struct reference metadata
- **graphs**: Complete function graphs with every node, pin, and connection
  - EventGraph nodes (custom events, dispatchers)
  - FunctionGraph nodes (full execution flow with branches, loops, function calls)

### 3. Generate AngelScript Code

Create AngelScript code that matches the blueprint exactly.

**Class Declaration:**
```angelscript
class A<ClassName> : A<ParentClass>
{
    // 1. Class defaults at the very top (from class_defaults array)
    default bReplicates = true;
    default NetUpdateFrequency = 10.0f;

    // 2. Components with their defaults
    // 3. Properties with inline defaults
    // 4. Functions
};
```

**Components with Default Properties:**
```angelscript
UPROPERTY(DefaultComponent, Attach = <ParentComponent>)
U<ComponentType> <ComponentName>;
default <ComponentName>.PropertyName = Value;
default <ComponentName>.AnotherProperty = Value;
```

**IMPORTANT - Component Default Placement:**
- Place `default` statements immediately after the component declaration
- Group all defaults for a component together
- This keeps properties with their components for better organization

**Properties with Inline Defaults:**
```angelscript
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
<Type> <PropertyName> = <DefaultValue>;
```

**IMPORTANT - Variable Default Strategy:**
- Use **inline initialization** for simple variable defaults: `int MyVar = 10;`
- Extract defaults from `cdo_default_value` field in the JSON
- Convert numeric types appropriately (use `f` suffix for floats)
- For blueprint references, note the `blueprint_reference` field but these often can't be set via defaults

**IMPORTANT - Variable Visibility (private vs UPROPERTY):**

Separate variables into two categories:

1. **Editor-configurable** — use `UPROPERTY(EditAnywhere, ...)`:
   - Tuneable values (speeds, distances, counts, classes)
   - Arrays that represent configuration or asset references
   - Sub-widget class references (`TSubclassOf<UMyWidget>`)

2. **Runtime state** — use bare `private` with no UPROPERTY macro:
   - Object references set at runtime (`Player`, `Container`, widget instances)
   - Transient state trackers (`HoverId`, `HoldId`, `bOpened`)
   - Initialize inline: `private APlayerCharacterAS Player = nullptr;`
   - `bool` state vars must use `b` prefix: `private bool bOpened = false;`

```angelscript
// Editor-configurable
UPROPERTY(EditAnywhere, Category = "Default")
TSubclassOf<UInventoryTipWidget> TipWidgetClass;

UPROPERTY(EditAnywhere, Category = "Default")
float AnimSpeed = 10.0f;

// Runtime state — no UPROPERTY, private
private UInventoryTipWidget TipWidget = nullptr;
private APlayerCharacterAS Player = nullptr;
private ASignalActor Container = nullptr;
private int HoverId = -1;
private int HoldId = -1;
private bool bOpened = false;
```

**Functions - Convert Graph Nodes to Code:**

For each function graph, trace the execution flow from the FunctionEntry node:
- Follow `exec` pin connections to determine statement order
- Convert K2Node_IfThenElse → `if/else` blocks
- Convert K2Node_MacroInstance (ForEachLoop) → `for` loops
- Convert K2Node_CallFunction → function calls
- Convert K2Node_VariableGet/Set → variable reads/writes
- Convert K2Node_SpawnActorFromClass → `SpawnActorUniqueNamed()` with deferred spawning for ExposeOnSpawn properties
- Convert K2Node_Select → ternary `? :` operator
- Convert K2Node_ExecutionSequence → sequential statements

Use existing codebase patterns (check Script/ folder for examples of SpawnActor, AttachToComponent, etc.)

### 4. Determine Output File Path

- If blueprint is at `Content/Path/To/BP_ClassName.uasset`
- AngelScript should be at `Script/Path/To/ClassName.as`
- Remove the `BP_` prefix from the filename

**Naming Conventions:**
- Blueprints: Remove `BP_` prefix (e.g., `BP_Player` → `APlayer`)
- Structs: Remove `S_` prefix, add `F` prefix (e.g., `S_Data` → `FData`)
- Widgets: Remove `W_` prefix, use `U` prefix, add `Widget` postfix (e.g., `W_InventorySlot` → `UInventorySlotWidget`)

### 5. Check for Existing File

Search for an existing AngelScript file with the same class name using Grep.

### 6. Ask User About File Creation/Update

Use AskUserQuestion to confirm:
- If file exists: Ask whether to overwrite, merge, or create new file
- If file doesn't exist: Ask to confirm the output path

### 7. Write the File

Create or update the AngelScript file with:
- Proper class structure matching the blueprint
- All properties with correct UPROPERTY macros
- All functions with implemented bodies (from graph analysis)
- All components with proper DefaultComponent/Attach specifications
- `// Generated from <BlueprintPath>` comment at the top

### 8. Report Results

Summarize what was generated:
- Class name and parent
- Number of properties, functions, components
- Which functions have full implementations vs TODO stubs
- File location as markdown link

## Tools Reference:

### MCP Tools (preferred):

| MCP Tool | Description |
|---|---|
| `mcp__blueprint-exporter__ping_editor` | Check if Unreal Editor is running |
| `mcp__blueprint-exporter__export_blueprint` | Export blueprint graph data (returns JSON directly) |
| `mcp__blueprint-exporter__list_blueprints` | List blueprints matching optional filter |
| `mcp__blueprint-exporter__export_struct` | Export UserDefinedStruct fields (returns JSON directly) |
| `mcp__blueprint-exporter__export_enum` | Export UserDefinedEnum values (returns JSON directly) |

### HTTP Endpoints (curl fallback):

| Endpoint | Description |
|---|---|
| `GET http://localhost:7233/ping` | Check if editor is running |
| `GET http://localhost:7233/export?path=/Game/...` | Export blueprint to JSON file |
| `GET http://localhost:7233/list?filter=...` | List blueprints matching filter |
| `GET http://localhost:7233/export-struct?path=/Game/...` | Export UserDefinedStruct to JSON file |
| `GET http://localhost:7233/export-enum?path=/Game/...` | Export UserDefinedEnum to JSON file |

Note: curl endpoints return `output_path` — you must then read the file. MCP tools return data directly.

## Converting Exported Data to AngelScript:

### Class Default Values (from `class_defaults`):

Class-level defaults should be placed at the **very top** of the class definition, before any property declarations:

```angelscript
class AAtmosphereVolume : AActor
{
    // Class defaults at the top
    default bReplicates = true;
    default bAlwaysRelevant = true;
    default NetUpdateFrequency = 10.0f;
    default bCanBeDamaged = false;

    // Then components, properties, and functions...
    UPROPERTY(DefaultComponent, RootComponent)
    UBoxComponent VolumeBox;
}
```

**From JSON:**
```json
{
  "class_defaults": [
    {
      "name": "bReplicates",
      "type": "bool",
      "value": "True"
    },
    {
      "name": "NetUpdateFrequency",
      "type": "float",
      "value": "10.000000"
    }
  ]
}
```

**Common Class Defaults:**
- **Replication**: `bReplicates`, `bAlwaysRelevant`, `bOnlyRelevantToOwner`, `NetUpdateFrequency`
- **Collision**: `bCanBeDamaged`, `bCollideWhenPlacing`, `bGenerateOverlapEvents`
- **Visibility**: `bHidden`, `bHiddenInGame`
- **Ticking**: `PrimaryActorTick.bCanEverTick`, `PrimaryActorTick.bStartWithTickEnabled`

### Variable Defaults (from `cdo_default_value`):
```angelscript
// JSON: "cdo_default_value": "6"
UPROPERTY(EditAnywhere)
int HandsItemCount = 6;

// JSON: "cdo_default_value": "31.000000"
UPROPERTY(EditAnywhere)
float HoldDist = 31.0f;

// JSON: "cdo_default_value": "False"
UPROPERTY(EditAnywhere)
bool Opened = false;
```

### Component Property Defaults:
```angelscript
UPROPERTY(DefaultComponent, Attach = InventoryNode)
UWidgetComponent InventoryWidget;
// From JSON component properties array:
default InventoryWidget.DrawSize = FVector2D(1015.0f, 765.0f);  // Note: DrawSize is FVector2D, not FIntPoint
default InventoryWidget.bVisible = false;
default InventoryWidget.RelativeLocation = FVector(1.242755f, 0.0f, 0.0f);
default InventoryWidget.RelativeRotation = FRotator(0.0f, -179.999999f, 0.0f);
default InventoryWidget.RelativeScale3D = FVector(0.0357f, 0.0357f, 0.0357f);
```

### Blueprint References:
When `is_blueprint_reference: true`:
```json
{
  "name": "WidgetClass",
  "value": "/Script/UMG.WidgetBlueprintGeneratedClass'/Game/Core/Inventory/W_Inventory.W_Inventory_C'",
  "blueprint_reference": "W_Inventory",
  "is_blueprint_reference": true
}
```

**Note:** Widget classes and other blueprint references typically can't be set via `default` statements in AngelScript. These are usually configured in the Blueprint editor and inherited automatically, or set in ConstructionScript if needed.

### Struct Detection and Handling:

The plugin automatically detects the source of struct types:

**Struct Source Types:**
```json
{
  "name": "MyStructVar",
  "type": "FMyCustomStruct",
  "struct_source": "blueprint",
  "is_struct_reference": true,
  "struct_reference": "MyCustomStruct"
}
```

**Handling Blueprint Structs:**

When you encounter a variable with `struct_source: "blueprint"`, export the struct definition:
- **MCP:** Use `mcp__blueprint-exporter__export_struct` with the struct's asset path
- **curl fallback:** `curl -s "http://localhost:7233/export-struct?path=/Game/Path/To/MyStruct"` then read the `output_path`

**Converting to AngelScript Struct:**
```angelscript
struct FMyCustomStruct
{
    float Health = 100.0f;
    float MaxHealth = 100.0f;
}
```

**IMPORTANT - Struct Naming Convention:**
- Remove `S_` prefix from UserDefinedStruct names (Blueprint convention)
- Replace with `F` prefix (Unreal C++ convention for structs)
- Example: `S_InventoryItem` → `FInventoryItem`
- Example: `S_BuildingData` → `FBuildingData`

**Strategy:**
1. Check if struct is already defined in AngelScript using Grep
2. For C++ structs (`struct_source: "cpp"`), they're usually available in AngelScript automatically
3. For blueprint structs (`struct_source: "blueprint"`):
   - Export the struct definition using MCP tool or curl fallback
   - Remove `S_` prefix and add `F` prefix for the AngelScript struct name
   - Check if AngelScript equivalent exists (search for the converted name)
   - If not, create the struct definition in an appropriate location (e.g., `Script/Core/Data/Structs.as`)
4. For nested structs, recursively check and export dependencies

### Type Conversions:
- JSON `IntPoint` values like `(X=1015,Y=765)` → `FVector2D(1015.0f, 765.0f)` for DrawSize
- JSON `Vector` values like `(X=1.0,Y=2.0,Z=3.0)` → `FVector(1.0f, 2.0f, 3.0f)`
- JSON `Rotator` values like `(Pitch=0.0,Yaw=180.0,Roll=0.0)` → `FRotator(0.0f, 180.0f, 0.0f)`
- JSON boolean `True`/`False` → `true`/`false`
- Always add `f` suffix to float literals

### Enum Detection and Handling:

When you encounter enum variables with `is_enum: true` and `enum_source: "blueprint"`, export the enum definition:
- **MCP:** Use `mcp__blueprint-exporter__export_enum` with the `enum_path` value
- **curl fallback:** `curl -s "http://localhost:7233/export-enum?path=/Game/Path/To/MyEnum"` then read the `output_path`

The tool returns JSON with the enum values directly:
```json
{
  "name": "EMyEnum",
  "enum_path": "/Game/Path/To/MyEnum",
  "enum_type": "UserDefinedEnum",
  "values": [
    { "name": "EMyEnum::Value1", "value": 0, "display_name": "Value 1" },
    { "name": "EMyEnum::Value2", "value": 1, "display_name": "Value 2" }
  ]
}
```

**Converting to AngelScript Enum:**
```angelscript
enum EMyEnum
{
    Value1,
    Value2
}
```

**IMPORTANT - Enum Naming Convention:**
- AngelScript enums must always have the `E` prefix
- If the UserDefinedEnum name starts with `E_`, replace `E_` with `E` (e.g., `E_SignalType` → `ESignalType`)
- If the name already starts with `E` (no underscore), keep it as-is (e.g., `ESignalType` stays `ESignalType`)
- If the name has no `E` prefix at all, add `E` (e.g., `MyEnum` → `EMyEnum`)
- This applies to both the enum definition name and all type references in code

**Strategy:**
1. Check if enum is already defined in AngelScript using Grep
2. For C++ enums, they're usually available in AngelScript automatically
3. For blueprint enums: export using MCP tool or curl fallback, check if AngelScript equivalent exists, create if not
4. Use the `display_name` field for clean enum value names when the internal name is auto-generated

### Naming Conversions:
- Blueprint classes: `BP_ClassName` → `AClassName` (remove `BP_` prefix)
- Widget blueprints: `W_ClassName` → `UClassNameWidget` (remove `W_` prefix, add `U` prefix and `Widget` postfix)
- UserDefinedStructs: `S_StructName` → `FStructName` (remove `S_`, add `F` prefix)
- UserDefinedEnums: `E_EnumName` → `EEnumName` (remove `E_` prefix, add `E` prefix)
- This applies to both definitions and type references in code

### Widget Blueprints (UUserWidget):

Widget blueprints (W_ prefix) convert to `UUserWidget` subclasses. Always add `UCLASS(Abstract)`.

**Class structure:**
```angelscript
UCLASS(Abstract)
class UInventorySlotWidget : UUserWidget
{
    // BindWidget - must match widget names in the UMG designer exactly
    UPROPERTY(BindWidget)
    UCanvasPanel Box;

    UPROPERTY(BindWidget)
    UBorder HoverBg;

    // Custom events become UFUNCTION() methods
    UFUNCTION()
    void Init(int HandSlot, bool AllowActions)
    {
        float Opacity = AllowActions ? 1.0f : 0.3f;
        Box.SetRenderOpacity(Opacity);
    }

    UFUNCTION()
    void Hover(bool IsHover)
    {
        ESlateVisibility Vis = IsHover ? ESlateVisibility::Visible : ESlateVisibility::Hidden;
        HoverBg.SetVisibility(Vis);
        HoverBorder.SetVisibility(Vis);
    }
}
```

**Key Widget patterns:**
- `UPROPERTY(BindWidget)` — declares a widget that must exist in the UMG layout with the same name
- `UPROPERTY(BindWidgetAnim)` — declares a widget animation that must exist in the UMG timeline with the same name (use `UWidgetAnimation` type)
- `UFUNCTION(BlueprintOverride) void Construct()` — called on widget creation, use to bind button delegates
- Button delegates: `MyButton.OnClicked.AddUFunction(this, n"MyHandler");`
- Widget output paths: `Content/Core/Inventory/W_Foo.uasset` → `Script/Core/Inventory/FooWidget.as` (strip `W_`, add `Widget` postfix)
- `K2Node_VariableSet` with `self` pin targeting a widget component = calling `SetVisibility()` / `SetRenderOpacity()` on that widget
- `K2Node_Select` with bool index = ternary: Option 0 is false branch, Option 1 is true branch
- `K2Node_ExecutionSequence` outputs with no connections = no-ops; only implement connected outputs
- ESlateVisibility enum values: `ESlateVisibility::Visible`, `ESlateVisibility::Hidden`, `ESlateVisibility::Collapsed`
- Widget animations use `PlayAnimation()` / `StopAnimation()` with the bound `UWidgetAnimation` reference

## Notes:

- The BlueprintExporter plugin starts automatically when Unreal Editor opens
- MCP server bridges Claude Code tools to the plugin's HTTP server (port 7233)
- The MCP tools return data directly — no need to read intermediate files
- Exports include complete graph data with all node connections
- Function implementations can be derived from the graph by tracing execution flow
- Use existing codebase patterns from Script/ for AngelScript idioms
- Component properties are exported only if they differ from the component class defaults (modified values only)
- Struct/enum source detection helps identify dependencies on UserDefinedStruct/Enum assets
- Always check if structs/enums already exist in AngelScript codebase before creating new definitions

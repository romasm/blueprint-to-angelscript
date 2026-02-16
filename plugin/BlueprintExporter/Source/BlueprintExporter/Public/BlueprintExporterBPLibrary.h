#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BlueprintExporterBPLibrary.generated.h"

UCLASS()
class BLUEPRINTEXPORTER_API UBlueprintExporterBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Export a blueprint's complete graph data to a JSON file.
	 * Includes variables, components, functions, event graphs, and all node connections.
	 *
	 * @param BlueprintPath - Asset path like "/Game/Core/Inventory/BP_InventoryVisual"
	 * @param OutputPath - Where to save the JSON file (empty = %TEMP%/blueprint_graph.json)
	 * @return true if export was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Exporter")
	static bool ExportBlueprintToJson(const FString& BlueprintPath, const FString& OutputPath = TEXT(""));

	/**
	 * Export a UserDefinedStruct's field definitions to a JSON file.
	 * Includes field names, types, and default values.
	 *
	 * @param StructPath - Asset path like "/Game/Data/Structs/MyStruct"
	 * @param OutputPath - Where to save the JSON file (empty = %TEMP%/struct.json)
	 * @return true if export was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Exporter")
	static bool ExportStructToJson(const FString& StructPath, const FString& OutputPath = TEXT(""));

	/**
	 * Export a UserDefinedEnum's values to a JSON file.
	 * Includes entry names, display names, and numeric values.
	 *
	 * @param EnumPath - Asset path like "/Game/Data/Enums/MyEnum"
	 * @param OutputPath - Where to save the JSON file (empty = %TEMP%/enum.json)
	 * @return true if export was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Exporter")
	static bool ExportEnumToJson(const FString& EnumPath, const FString& OutputPath = TEXT(""));
};

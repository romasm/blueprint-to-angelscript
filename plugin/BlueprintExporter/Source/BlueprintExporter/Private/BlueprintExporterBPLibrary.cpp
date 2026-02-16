#include "BlueprintExporterBPLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/UserDefinedStruct.h"
#include "Engine/UserDefinedEnum.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_TemporaryVariable.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/PropertyIterator.h"

static FString ExtractBlueprintPath(const FString& ValueStr)
{
	// Parse blueprint reference strings like:
	// "WidgetBlueprintGeneratedClass'/Game/UI/W_Inventory.W_Inventory_C'"
	// "BlueprintGeneratedClass'/Game/Actors/BP_MyActor.BP_MyActor_C'"

	int32 StartIdx = ValueStr.Find(TEXT("'/Game/"));
	if (StartIdx == INDEX_NONE)
		StartIdx = ValueStr.Find(TEXT("'/Script/"));

	if (StartIdx != INDEX_NONE)
	{
		int32 EndIdx = ValueStr.Find(TEXT("'"), ESearchCase::IgnoreCase, ESearchDir::FromStart, StartIdx + 2);
		if (EndIdx != INDEX_NONE)
		{
			FString FullPath = ValueStr.Mid(StartIdx + 1, EndIdx - StartIdx - 1);

			// Remove the _C suffix if present
			if (FullPath.EndsWith(TEXT("_C")))
			{
				FullPath = FullPath.LeftChop(2);
			}

			// Extract just the blueprint name (last part after the dot)
			FString BlueprintName;
			if (FullPath.Split(TEXT("."), nullptr, &BlueprintName, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
			{
				return BlueprintName;
			}

			// If no dot found, extract from the path
			int32 LastSlash = INDEX_NONE;
			FullPath.FindLastChar('/', LastSlash);
			if (LastSlash != INDEX_NONE)
			{
				return FullPath.Mid(LastSlash + 1);
			}

			return FullPath;
		}
	}

	return FString();
}

static bool IsBlueprintReference(const FString& ValueStr)
{
	return (ValueStr.Contains(TEXT("Blueprint")) || ValueStr.Contains(TEXT("WidgetBlueprint"))) &&
	       (ValueStr.Contains(TEXT("'/Game/")) || ValueStr.Contains(TEXT("'/Script/")));
}

static bool IsUserDefinedStruct(const FString& ValueStr)
{
	return ValueStr.Contains(TEXT("UserDefinedStruct"));
}

static FString GetStructSource(FStructProperty* StructProp)
{
	if (!StructProp || !StructProp->Struct)
		return TEXT("unknown");

	UScriptStruct* Struct = StructProp->Struct;

	// Check if it's a blueprint struct (UserDefinedStruct)
	if (Struct->IsA<UUserDefinedStruct>())
		return TEXT("blueprint");

	// Check if it's from engine/C++
	FString PackageName = Struct->GetOutermost()->GetName();
	if (PackageName.StartsWith(TEXT("/Script/")))
		return TEXT("cpp");

	// Custom or plugin struct
	return TEXT("custom");
}

static FString GetPinTypeString(const FEdGraphPinType& PinType)
{
	FString TypeStr;

	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
		TypeStr = TEXT("bool");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
		TypeStr = TEXT("int32");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int64)
		TypeStr = TEXT("int64");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Float || PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
		TypeStr = TEXT("float");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Double)
		TypeStr = TEXT("float64");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_String)
		TypeStr = TEXT("FString");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
		TypeStr = TEXT("FName");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
		TypeStr = TEXT("FText");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		if (UEnum* Enum = Cast<UEnum>(PinType.PinSubCategoryObject.Get()))
			TypeStr = Enum->GetName();
		else
			TypeStr = TEXT("uint8");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		if (UScriptStruct* Struct = Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get()))
			TypeStr = Struct->GetName();
		else
			TypeStr = TEXT("Struct");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object || PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject)
	{
		if (UClass* Class = Cast<UClass>(PinType.PinSubCategoryObject.Get()))
			TypeStr = Class->GetName();
		else
			TypeStr = TEXT("UObject");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Class || PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
	{
		if (UClass* Class = Cast<UClass>(PinType.PinSubCategoryObject.Get()))
			TypeStr = FString::Printf(TEXT("TSubclassOf<%s>"), *Class->GetName());
		else
			TypeStr = TEXT("TSubclassOf<UObject>");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Enum)
	{
		if (UEnum* Enum = Cast<UEnum>(PinType.PinSubCategoryObject.Get()))
			TypeStr = Enum->GetName();
		else
			TypeStr = TEXT("Enum");
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		TypeStr = TEXT("exec");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
		TypeStr = TEXT("Delegate");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate)
		TypeStr = TEXT("MulticastDelegate");
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
		TypeStr = TEXT("Wildcard");
	else
		TypeStr = PinType.PinCategory.ToString();

	if (PinType.IsArray())
		TypeStr = FString::Printf(TEXT("TArray<%s>"), *TypeStr);
	else if (PinType.IsSet())
		TypeStr = FString::Printf(TEXT("TSet<%s>"), *TypeStr);
	else if (PinType.IsMap())
		TypeStr = FString::Printf(TEXT("TMap<%s, Value>"), *TypeStr);

	if (PinType.bIsReference)
		TypeStr += TEXT("&");

	return TypeStr;
}

static TSharedPtr<FJsonObject> ExportPin(const UEdGraphPin* Pin)
{
	TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject());

	PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
	PinObj->SetStringField(TEXT("type"), GetPinTypeString(Pin->PinType));
	PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));

	if (!Pin->DefaultValue.IsEmpty())
		PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);

	if (!Pin->DefaultTextValue.IsEmpty())
		PinObj->SetStringField(TEXT("default_text"), Pin->DefaultTextValue.ToString());

	if (Pin->DefaultObject)
		PinObj->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetName());

	// Export connections
	TArray<TSharedPtr<FJsonValue>> Connections;
	for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
	{
		if (LinkedPin && LinkedPin->GetOwningNode())
		{
			TSharedPtr<FJsonObject> ConnObj = MakeShareable(new FJsonObject());
			ConnObj->SetStringField(TEXT("node"), LinkedPin->GetOwningNode()->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			ConnObj->SetStringField(TEXT("node_name"), LinkedPin->GetOwningNode()->GetName());
			ConnObj->SetStringField(TEXT("pin"), LinkedPin->PinName.ToString());
			Connections.Add(MakeShareable(new FJsonValueObject(ConnObj)));
		}
	}
	if (Connections.Num() > 0)
		PinObj->SetArrayField(TEXT("connections"), Connections);

	return PinObj;
}

static TSharedPtr<FJsonObject> ExportNode(const UEdGraphNode* Node)
{
	TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject());

	NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
	NodeObj->SetStringField(TEXT("name"), Node->GetName());
	NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	NodeObj->SetStringField(TEXT("compact_title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());

	if (!Node->NodeComment.IsEmpty())
		NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);

	// Node-specific data
	if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
	{
		NodeObj->SetStringField(TEXT("node_type"), TEXT("CallFunction"));
		NodeObj->SetStringField(TEXT("function_name"), CallNode->FunctionReference.GetMemberName().ToString());
		if (UClass* MemberParent = CallNode->FunctionReference.GetMemberParentClass())
			NodeObj->SetStringField(TEXT("target_class"), MemberParent->GetName());
	}
	else if (const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
	{
		NodeObj->SetStringField(TEXT("node_type"), TEXT("Event"));
		NodeObj->SetStringField(TEXT("event_name"), EventNode->EventReference.GetMemberName().ToString());
	}
	else if (const UK2Node_CustomEvent* CustomEventNode = Cast<UK2Node_CustomEvent>(Node))
	{
		NodeObj->SetStringField(TEXT("node_type"), TEXT("CustomEvent"));
		NodeObj->SetStringField(TEXT("event_name"), CustomEventNode->CustomFunctionName.ToString());
	}
	else if (const UK2Node_VariableGet* VarGetNode = Cast<UK2Node_VariableGet>(Node))
	{
		NodeObj->SetStringField(TEXT("node_type"), TEXT("VariableGet"));
		NodeObj->SetStringField(TEXT("variable_name"), VarGetNode->VariableReference.GetMemberName().ToString());
	}
	else if (const UK2Node_VariableSet* VarSetNode = Cast<UK2Node_VariableSet>(Node))
	{
		NodeObj->SetStringField(TEXT("node_type"), TEXT("VariableSet"));
		NodeObj->SetStringField(TEXT("variable_name"), VarSetNode->VariableReference.GetMemberName().ToString());
	}
	else if (const UK2Node_IfThenElse* BranchNode = Cast<UK2Node_IfThenElse>(Node))
	{
		NodeObj->SetStringField(TEXT("node_type"), TEXT("Branch"));
	}
	else if (const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
	{
		NodeObj->SetStringField(TEXT("node_type"), TEXT("Cast"));
		if (CastNode->TargetType)
			NodeObj->SetStringField(TEXT("target_type"), CastNode->TargetType->GetName());
	}
	else if (const UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
	{
		NodeObj->SetStringField(TEXT("node_type"), TEXT("FunctionEntry"));
	}
	else if (const UK2Node_FunctionResult* ResultNode = Cast<UK2Node_FunctionResult>(Node))
	{
		NodeObj->SetStringField(TEXT("node_type"), TEXT("FunctionResult"));
	}
	else if (const UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
	{
		NodeObj->SetStringField(TEXT("node_type"), TEXT("Macro"));
		if (MacroNode->GetMacroGraph())
			NodeObj->SetStringField(TEXT("macro_name"), MacroNode->GetMacroGraph()->GetName());
	}
	else
	{
		NodeObj->SetStringField(TEXT("node_type"), Node->GetClass()->GetName());
	}

	// Export all pins
	TArray<TSharedPtr<FJsonValue>> InputPins;
	TArray<TSharedPtr<FJsonValue>> OutputPins;

	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->bHidden)
			continue;

		TSharedPtr<FJsonObject> PinObj = ExportPin(Pin);

		if (Pin->Direction == EGPD_Input)
			InputPins.Add(MakeShareable(new FJsonValueObject(PinObj)));
		else
			OutputPins.Add(MakeShareable(new FJsonValueObject(PinObj)));
	}

	NodeObj->SetArrayField(TEXT("inputs"), InputPins);
	NodeObj->SetArrayField(TEXT("outputs"), OutputPins);

	return NodeObj;
}

bool UBlueprintExporterBPLibrary::ExportBlueprintToJson(const FString& BlueprintPath, const FString& OutputPath)
{
	// Load the blueprint
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("BlueprintExporter: Could not load blueprint at %s"), *BlueprintPath);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("BlueprintExporter: Loaded %s"), *Blueprint->GetName());

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject());

	Root->SetStringField(TEXT("name"), Blueprint->GetName());
	Root->SetStringField(TEXT("blueprint_path"), BlueprintPath);

	// Parent class
	if (Blueprint->ParentClass)
		Root->SetStringField(TEXT("parent_class"), Blueprint->ParentClass->GetName());

	// ---- Class Default Values ----
	// Export CDO properties that differ from parent class defaults
	TArray<TSharedPtr<FJsonValue>> ClassDefaults;

	if (Blueprint->GeneratedClass && Blueprint->ParentClass)
	{
		UObject* ClassCDO = Blueprint->GeneratedClass->GetDefaultObject();
		UObject* ParentCDO = Blueprint->ParentClass->GetDefaultObject();

		if (ClassCDO && ParentCDO)
		{
			// Get list of blueprint variable names to skip (handled separately)
			TSet<FName> BlueprintVariableNames;
			for (const FBPVariableDescription& Var : Blueprint->NewVariables)
			{
				BlueprintVariableNames.Add(Var.VarName);
			}

			for (TFieldIterator<FProperty> PropIt(Blueprint->GeneratedClass); PropIt; ++PropIt)
			{
				FProperty* Property = *PropIt;

				// Skip transient properties
				if (!Property || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
					continue;

				// Skip blueprint variables (handled in variables section)
				if (BlueprintVariableNames.Contains(Property->GetFName()))
					continue;

				// Skip component properties (handled in components section)
				if (CastField<FObjectProperty>(Property) && Property->GetName().StartsWith(TEXT("K2Node_")))
					continue;

				// Only export if property exists in parent class and differs
				FProperty* ParentProperty = Blueprint->ParentClass->FindPropertyByName(Property->GetFName());
				if (!ParentProperty)
					continue;

				const void* ChildValue = Property->ContainerPtrToValuePtr<void>(ClassCDO);
				const void* ParentValue = ParentProperty->ContainerPtrToValuePtr<void>(ParentCDO);

				// Compare values - only export if different
				if (!Property->Identical(ChildValue, ParentValue))
				{
					TSharedPtr<FJsonObject> DefaultObj = MakeShareable(new FJsonObject());
					DefaultObj->SetStringField(TEXT("name"), Property->GetName());

					FString ValueStr;
					Property->ExportTextItem_Direct(ValueStr, ChildValue, nullptr, nullptr, PPF_None);
					DefaultObj->SetStringField(TEXT("value"), ValueStr);

					// Get property type
					FString TypeStr;
					if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
					{
						TypeStr = StructProp->Struct->GetName();

						// Add struct source information
						FString StructSource = GetStructSource(StructProp);
						DefaultObj->SetStringField(TEXT("struct_source"), StructSource);

						if (StructSource == TEXT("blueprint") && IsUserDefinedStruct(ValueStr))
						{
							FString StructName = ExtractBlueprintPath(ValueStr);
							if (!StructName.IsEmpty())
							{
								DefaultObj->SetStringField(TEXT("struct_reference"), StructName);
								DefaultObj->SetBoolField(TEXT("is_struct_reference"), true);
							}
						}
					}
					else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
						TypeStr = ObjProp->PropertyClass->GetName();
					else if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
						TypeStr = FString::Printf(TEXT("TSubclassOf<%s>"), *ClassProp->MetaClass->GetName());
					else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
					{
						if (UEnum* Enum = EnumProp->GetEnum())
							TypeStr = Enum->GetName();
						else
							TypeStr = TEXT("uint8");
					}
					else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
					{
						if (ByteProp->Enum)
							TypeStr = ByteProp->Enum->GetName();
						else
							TypeStr = TEXT("uint8");
					}
					else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
						TypeStr = TEXT("bool");
					else
						TypeStr = Property->GetCPPType();

					DefaultObj->SetStringField(TEXT("type"), TypeStr);

					// Check if this is a blueprint reference
					if (IsBlueprintReference(ValueStr))
					{
						FString BPName = ExtractBlueprintPath(ValueStr);
						if (!BPName.IsEmpty())
						{
							DefaultObj->SetStringField(TEXT("blueprint_reference"), BPName);
							DefaultObj->SetBoolField(TEXT("is_blueprint_reference"), true);
						}
					}

					ClassDefaults.Add(MakeShareable(new FJsonValueObject(DefaultObj)));
					UE_LOG(LogTemp, Log, TEXT("  Class Default: %s = %s"), *Property->GetName(), *ValueStr);
				}
			}
		}
	}

	if (ClassDefaults.Num() > 0)
	{
		Root->SetArrayField(TEXT("class_defaults"), ClassDefaults);
		UE_LOG(LogTemp, Log, TEXT("  Exported %d class default values"), ClassDefaults.Num());
	}

	// ---- Variables ----
	TArray<TSharedPtr<FJsonValue>> Variables;

	// Get CDO (Class Default Object) to extract actual default values
	UObject* CDO = nullptr;
	if (Blueprint->GeneratedClass)
	{
		CDO = Blueprint->GeneratedClass->GetDefaultObject();
	}

	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShareable(new FJsonObject());
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), GetPinTypeString(Var.VarType));
		VarObj->SetStringField(TEXT("category"), Var.Category.ToString());
		VarObj->SetStringField(TEXT("default_value"), Var.DefaultValue);
		VarObj->SetStringField(TEXT("friendly_name"), Var.FriendlyName);

		// Extract actual default value from CDO
		if (CDO && Blueprint->GeneratedClass)
		{
			FProperty* Property = Blueprint->GeneratedClass->FindPropertyByName(Var.VarName);
			if (Property)
			{
				// Add struct source information for struct properties
				if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
				{
					FString StructSource = GetStructSource(StructProp);
					VarObj->SetStringField(TEXT("struct_source"), StructSource);
				}

				// Add enum source information for enum properties
				UEnum* VarEnum = nullptr;
				if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
					VarEnum = EnumProp->GetEnum();
				else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
					VarEnum = ByteProp->Enum;

				if (VarEnum)
				{
					VarObj->SetBoolField(TEXT("is_enum"), true);
					VarObj->SetStringField(TEXT("enum_name"), VarEnum->GetName());
					// Check if it's a UserDefinedEnum (blueprint enum)
					if (Cast<UUserDefinedEnum>(VarEnum))
					{
						VarObj->SetStringField(TEXT("enum_source"), TEXT("blueprint"));
						VarObj->SetStringField(TEXT("enum_path"), VarEnum->GetPathName());
					}
					else
					{
						VarObj->SetStringField(TEXT("enum_source"), TEXT("cpp"));
					}
				}

				FString CDOValue;
				const void* PropertyAddr = Property->ContainerPtrToValuePtr<void>(CDO);
				Property->ExportTextItem_Direct(CDOValue, PropertyAddr, nullptr, nullptr, PPF_None);

				if (!CDOValue.IsEmpty())
				{
					VarObj->SetStringField(TEXT("cdo_default_value"), CDOValue);

					// Check if this is a blueprint reference
					if (IsBlueprintReference(CDOValue))
					{
						FString BPName = ExtractBlueprintPath(CDOValue);
						if (!BPName.IsEmpty())
						{
							VarObj->SetStringField(TEXT("blueprint_reference"), BPName);
							VarObj->SetBoolField(TEXT("is_blueprint_reference"), true);
						}
					}

					// Check if this is a user-defined struct reference
					if (IsUserDefinedStruct(CDOValue))
					{
						FString StructName = ExtractBlueprintPath(CDOValue);
						if (!StructName.IsEmpty())
						{
							VarObj->SetStringField(TEXT("struct_reference"), StructName);
							VarObj->SetBoolField(TEXT("is_struct_reference"), true);
						}
					}
				}
			}
		}

		// Property flags
		TArray<FString> Flags;
		if (Var.PropertyFlags & CPF_Edit) Flags.Add(TEXT("EditAnywhere"));
		if (Var.PropertyFlags & CPF_BlueprintVisible) Flags.Add(TEXT("BlueprintVisible"));
		if (Var.PropertyFlags & CPF_BlueprintReadOnly) Flags.Add(TEXT("BlueprintReadOnly"));
		if (Var.PropertyFlags & CPF_ExposeOnSpawn) Flags.Add(TEXT("ExposeOnSpawn"));
		if (Var.PropertyFlags & CPF_Interp) Flags.Add(TEXT("Interp"));

		VarObj->SetStringField(TEXT("flags"), FString::Join(Flags, TEXT(", ")));

		Variables.Add(MakeShareable(new FJsonValueObject(VarObj)));
		UE_LOG(LogTemp, Log, TEXT("  Variable: %s (%s)"), *Var.VarName.ToString(), *GetPinTypeString(Var.VarType));
	}
	Root->SetArrayField(TEXT("variables"), Variables);

	// ---- Components ----
	TArray<TSharedPtr<FJsonValue>> Components;
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* SCSNode : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (!SCSNode || !SCSNode->ComponentTemplate)
				continue;

			TSharedPtr<FJsonObject> CompObj = MakeShareable(new FJsonObject());
			CompObj->SetStringField(TEXT("name"), SCSNode->GetVariableName().ToString());
			CompObj->SetStringField(TEXT("type"), SCSNode->ComponentTemplate->GetClass()->GetName());

			if (SCSNode->ParentComponentOrVariableName != NAME_None)
				CompObj->SetStringField(TEXT("attach_parent"), SCSNode->ParentComponentOrVariableName.ToString());

			// Export component properties
			TArray<TSharedPtr<FJsonValue>> Properties;
			UObject* ComponentTemplate = SCSNode->ComponentTemplate;
			UClass* ComponentClass = ComponentTemplate->GetClass();

			// Get the CDO to compare against defaults
			UObject* ComponentCDO = ComponentClass->GetDefaultObject();

			for (TFieldIterator<FProperty> PropIt(ComponentClass); PropIt; ++PropIt)
			{
				FProperty* Property = *PropIt;

				// Skip properties that shouldn't be exported
				if (!Property || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
					continue;

				// Only export properties that differ from CDO defaults
				const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ComponentTemplate);
				const void* DefaultPtr = Property->ContainerPtrToValuePtr<void>(ComponentCDO);

				if (!Property->Identical(ValuePtr, DefaultPtr))
				{
					TSharedPtr<FJsonObject> PropObj = MakeShareable(new FJsonObject());
					PropObj->SetStringField(TEXT("name"), Property->GetName());

					FString ValueStr;
					Property->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);
					PropObj->SetStringField(TEXT("value"), ValueStr);

					// Get property type
					FString TypeStr;
					if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
					{
						TypeStr = StructProp->Struct->GetName();

						// Add struct source information
						FString StructSource = GetStructSource(StructProp);
						PropObj->SetStringField(TEXT("struct_source"), StructSource);

						// If it's a blueprint struct, check if value contains the path
						if (StructSource == TEXT("blueprint") && IsUserDefinedStruct(ValueStr))
						{
							FString StructName = ExtractBlueprintPath(ValueStr);
							if (!StructName.IsEmpty())
							{
								PropObj->SetStringField(TEXT("struct_reference"), StructName);
								PropObj->SetBoolField(TEXT("is_struct_reference"), true);
							}
						}
					}
					else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
						TypeStr = ObjProp->PropertyClass->GetName();
					else if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
						TypeStr = FString::Printf(TEXT("TSubclassOf<%s>"), *ClassProp->MetaClass->GetName());
					else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
					{
						if (UEnum* Enum = EnumProp->GetEnum())
							TypeStr = Enum->GetName();
						else
							TypeStr = TEXT("uint8");
					}
					else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
					{
						if (ByteProp->Enum)
							TypeStr = ByteProp->Enum->GetName();
						else
							TypeStr = TEXT("uint8");
					}
					else
						TypeStr = Property->GetCPPType();

					PropObj->SetStringField(TEXT("type"), TypeStr);

					// Check if this is a blueprint reference
					if (IsBlueprintReference(ValueStr))
					{
						FString BPName = ExtractBlueprintPath(ValueStr);
						if (!BPName.IsEmpty())
						{
							PropObj->SetStringField(TEXT("blueprint_reference"), BPName);
							PropObj->SetBoolField(TEXT("is_blueprint_reference"), true);
						}
					}

					Properties.Add(MakeShareable(new FJsonValueObject(PropObj)));
				}
			}

			if (Properties.Num() > 0)
				CompObj->SetArrayField(TEXT("properties"), Properties);

			Components.Add(MakeShareable(new FJsonValueObject(CompObj)));
			UE_LOG(LogTemp, Log, TEXT("  Component: %s (%s) - %d modified properties"),
				*SCSNode->GetVariableName().ToString(),
				*SCSNode->ComponentTemplate->GetClass()->GetName(),
				Properties.Num());
		}
	}
	Root->SetArrayField(TEXT("components"), Components);

	// ---- Event Dispatchers ----
	TArray<TSharedPtr<FJsonValue>> Dispatchers;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate)
		{
			Dispatchers.Add(MakeShareable(new FJsonValueString(Var.VarName.ToString())));
		}
	}
	Root->SetArrayField(TEXT("event_dispatchers"), Dispatchers);

	// ---- Graphs ----
	TArray<TSharedPtr<FJsonValue>> Graphs;

	// Event Graphs (UberGraphPages)
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (!Graph) continue;

		TSharedPtr<FJsonObject> GraphObj = MakeShareable(new FJsonObject());
		GraphObj->SetStringField(TEXT("name"), Graph->GetName());
		GraphObj->SetStringField(TEXT("type"), TEXT("EventGraph"));

		TArray<TSharedPtr<FJsonValue>> Nodes;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			Nodes.Add(MakeShareable(new FJsonValueObject(ExportNode(Node))));
		}
		GraphObj->SetArrayField(TEXT("nodes"), Nodes);

		Graphs.Add(MakeShareable(new FJsonValueObject(GraphObj)));
		UE_LOG(LogTemp, Log, TEXT("  EventGraph: %s (%d nodes)"), *Graph->GetName(), Graph->Nodes.Num());
	}

	// Function Graphs
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph) continue;

		TSharedPtr<FJsonObject> GraphObj = MakeShareable(new FJsonObject());
		GraphObj->SetStringField(TEXT("name"), Graph->GetName());
		GraphObj->SetStringField(TEXT("type"), TEXT("FunctionGraph"));

		TArray<TSharedPtr<FJsonValue>> Nodes;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			Nodes.Add(MakeShareable(new FJsonValueObject(ExportNode(Node))));
		}
		GraphObj->SetArrayField(TEXT("nodes"), Nodes);

		Graphs.Add(MakeShareable(new FJsonValueObject(GraphObj)));
		UE_LOG(LogTemp, Log, TEXT("  FunctionGraph: %s (%d nodes)"), *Graph->GetName(), Graph->Nodes.Num());
	}

	Root->SetArrayField(TEXT("graphs"), Graphs);

	// ---- Write JSON ----
	FString FinalOutputPath = OutputPath;
	if (FinalOutputPath.IsEmpty())
	{
		FString BPName = FPaths::GetBaseFilename(BlueprintPath);
		FinalOutputPath = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("TEMP")), BPName + TEXT(".json"));
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	if (FFileHelper::SaveStringToFile(OutputString, *FinalOutputPath))
	{
		UE_LOG(LogTemp, Log, TEXT("BlueprintExporter: Exported to %s"), *FinalOutputPath);
		UE_LOG(LogTemp, Log, TEXT("  Variables: %d"), Variables.Num());
		UE_LOG(LogTemp, Log, TEXT("  Components: %d"), Components.Num());
		UE_LOG(LogTemp, Log, TEXT("  Graphs: %d"), Graphs.Num());
		return true;
	}

	UE_LOG(LogTemp, Error, TEXT("BlueprintExporter: Failed to write file %s"), *FinalOutputPath);
	return false;
}

bool UBlueprintExporterBPLibrary::ExportStructToJson(const FString& StructPath, const FString& OutputPath)
{
	// Load the struct
	UUserDefinedStruct* Struct = LoadObject<UUserDefinedStruct>(nullptr, *StructPath);
	if (!Struct)
	{
		UE_LOG(LogTemp, Error, TEXT("BlueprintExporter: Could not load struct at %s"), *StructPath);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("BlueprintExporter: Loaded struct %s"), *Struct->GetName());

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject());

	Root->SetStringField(TEXT("name"), Struct->GetName());
	Root->SetStringField(TEXT("struct_path"), StructPath);
	Root->SetStringField(TEXT("struct_type"), TEXT("UserDefinedStruct"));

	// Export struct fields
	TArray<TSharedPtr<FJsonValue>> Fields;
	for (TFieldIterator<FProperty> PropIt(Struct); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property)
			continue;

		TSharedPtr<FJsonObject> FieldObj = MakeShareable(new FJsonObject());
		FieldObj->SetStringField(TEXT("name"), Property->GetName());

		// Get property type
		FString TypeStr;
		if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
		{
			TypeStr = StructProp->Struct->GetName();
			FString StructSource = GetStructSource(StructProp);
			FieldObj->SetStringField(TEXT("struct_source"), StructSource);
		}
		else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
			TypeStr = ObjProp->PropertyClass->GetName();
		else if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
			TypeStr = FString::Printf(TEXT("TSubclassOf<%s>"), *ClassProp->MetaClass->GetName());
		else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
		{
			if (UEnum* Enum = EnumProp->GetEnum())
				TypeStr = Enum->GetName();
			else
				TypeStr = TEXT("uint8");
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
		{
			if (ByteProp->Enum)
				TypeStr = ByteProp->Enum->GetName();
			else
				TypeStr = TEXT("uint8");
		}
		else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
		{
			FProperty* InnerProp = ArrayProp->Inner;
			if (FStructProperty* InnerStructProp = CastField<FStructProperty>(InnerProp))
				TypeStr = FString::Printf(TEXT("TArray<%s>"), *InnerStructProp->Struct->GetName());
			else
				TypeStr = FString::Printf(TEXT("TArray<%s>"), *InnerProp->GetCPPType());
		}
		else
			TypeStr = Property->GetCPPType();

		FieldObj->SetStringField(TEXT("type"), TypeStr);

		// Get default value from struct's default instance
		const void* DefaultValuePtr = Property->ContainerPtrToValuePtr<void>(Struct->GetDefaultInstance());
		if (DefaultValuePtr)
		{
			FString DefaultValueStr;
			Property->ExportTextItem_Direct(DefaultValueStr, DefaultValuePtr, nullptr, nullptr, PPF_None);
			if (!DefaultValueStr.IsEmpty())
			{
				FieldObj->SetStringField(TEXT("default_value"), DefaultValueStr);
			}
		}

		Fields.Add(MakeShareable(new FJsonValueObject(FieldObj)));
		UE_LOG(LogTemp, Log, TEXT("  Field: %s (%s)"), *Property->GetName(), *TypeStr);
	}

	Root->SetArrayField(TEXT("fields"), Fields);

	// Write JSON
	FString FinalOutputPath = OutputPath;
	if (FinalOutputPath.IsEmpty())
	{
		FString StructName = FPaths::GetBaseFilename(StructPath);
		FinalOutputPath = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("TEMP")), StructName + TEXT("_struct.json"));
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	if (FFileHelper::SaveStringToFile(OutputString, *FinalOutputPath))
	{
		UE_LOG(LogTemp, Log, TEXT("BlueprintExporter: Exported struct to %s"), *FinalOutputPath);
		UE_LOG(LogTemp, Log, TEXT("  Fields: %d"), Fields.Num());
		return true;
	}

	UE_LOG(LogTemp, Error, TEXT("BlueprintExporter: Failed to write struct file %s"), *FinalOutputPath);
	return false;
}

bool UBlueprintExporterBPLibrary::ExportEnumToJson(const FString& EnumPath, const FString& OutputPath)
{
	// Load the enum
	UUserDefinedEnum* Enum = LoadObject<UUserDefinedEnum>(nullptr, *EnumPath);
	if (!Enum)
	{
		UE_LOG(LogTemp, Error, TEXT("BlueprintExporter: Could not load enum at %s"), *EnumPath);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("BlueprintExporter: Loaded enum %s"), *Enum->GetName());

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject());

	Root->SetStringField(TEXT("name"), Enum->GetName());
	Root->SetStringField(TEXT("enum_path"), EnumPath);
	Root->SetStringField(TEXT("enum_type"), TEXT("UserDefinedEnum"));

	// Export enum values
	TArray<TSharedPtr<FJsonValue>> Values;
	int32 MaxIndex = Enum->NumEnums();

	for (int32 i = 0; i < MaxIndex; i++)
	{
		// Skip the _MAX entry that UE auto-generates
		FString Name = Enum->GetNameStringByIndex(i);
		if (Name.EndsWith(TEXT("_MAX")))
		{
			continue;
		}

		TSharedPtr<FJsonObject> EntryObj = MakeShareable(new FJsonObject());
		EntryObj->SetStringField(TEXT("name"), Name);
		EntryObj->SetNumberField(TEXT("value"), static_cast<double>(Enum->GetValueByIndex(i)));

		// Get the display name (user-friendly name set in editor)
		FText DisplayName = Enum->GetDisplayNameTextByIndex(i);
		EntryObj->SetStringField(TEXT("display_name"), DisplayName.ToString());

		Values.Add(MakeShareable(new FJsonValueObject(EntryObj)));
		UE_LOG(LogTemp, Log, TEXT("  Entry: %s = %lld (Display: %s)"), *Name, Enum->GetValueByIndex(i), *DisplayName.ToString());
	}

	Root->SetArrayField(TEXT("values"), Values);

	// Write JSON
	FString FinalOutputPath = OutputPath;
	if (FinalOutputPath.IsEmpty())
	{
		FString EnumName = FPaths::GetBaseFilename(EnumPath);
		FinalOutputPath = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("TEMP")), EnumName + TEXT("_enum.json"));
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	if (FFileHelper::SaveStringToFile(OutputString, *FinalOutputPath))
	{
		UE_LOG(LogTemp, Log, TEXT("BlueprintExporter: Exported enum to %s"), *FinalOutputPath);
		UE_LOG(LogTemp, Log, TEXT("  Values: %d"), Values.Num());
		return true;
	}

	UE_LOG(LogTemp, Error, TEXT("BlueprintExporter: Failed to write enum file %s"), *FinalOutputPath);
	return false;
}

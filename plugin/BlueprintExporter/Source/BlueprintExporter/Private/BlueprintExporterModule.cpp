#include "Modules/ModuleManager.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpPath.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "BlueprintExporterBPLibrary.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"

static const uint32 BLUEPRINT_EXPORTER_PORT = 7233;

class FBlueprintExporterModule : public IModuleInterface
{
	FHttpRouteHandle ExportRouteHandle;
	FHttpRouteHandle PingRouteHandle;
	FHttpRouteHandle ListRouteHandle;
	FHttpRouteHandle ExportStructRouteHandle;
	FHttpRouteHandle ExportEnumRouteHandle;

public:
	virtual void StartupModule() override
	{
		FHttpServerModule& HttpServerModule = FHttpServerModule::Get();
		TSharedPtr<IHttpRouter> Router = HttpServerModule.GetHttpRouter(BLUEPRINT_EXPORTER_PORT);

		if (!Router)
		{
			UE_LOG(LogTemp, Error, TEXT("BlueprintExporter: Failed to get HTTP router on port %d"), BLUEPRINT_EXPORTER_PORT);
			return;
		}

		PingRouteHandle = Router->BindRoute(
			FHttpPath(TEXT("/ping")),
			EHttpServerRequestVerbs::VERB_GET,
			FHttpRequestHandler::CreateRaw(this, &FBlueprintExporterModule::HandlePing)
		);

		ExportRouteHandle = Router->BindRoute(
			FHttpPath(TEXT("/export")),
			EHttpServerRequestVerbs::VERB_GET,
			FHttpRequestHandler::CreateRaw(this, &FBlueprintExporterModule::HandleExport)
		);

		ListRouteHandle = Router->BindRoute(
			FHttpPath(TEXT("/list")),
			EHttpServerRequestVerbs::VERB_GET,
			FHttpRequestHandler::CreateRaw(this, &FBlueprintExporterModule::HandleList)
		);

		ExportStructRouteHandle = Router->BindRoute(
			FHttpPath(TEXT("/export-struct")),
			EHttpServerRequestVerbs::VERB_GET,
			FHttpRequestHandler::CreateRaw(this, &FBlueprintExporterModule::HandleExportStruct)
		);

		ExportEnumRouteHandle = Router->BindRoute(
			FHttpPath(TEXT("/export-enum")),
			EHttpServerRequestVerbs::VERB_GET,
			FHttpRequestHandler::CreateRaw(this, &FBlueprintExporterModule::HandleExportEnum)
		);

		HttpServerModule.StartAllListeners();

		UE_LOG(LogTemp, Log, TEXT("BlueprintExporter: HTTP server started on port %d"), BLUEPRINT_EXPORTER_PORT);
		UE_LOG(LogTemp, Log, TEXT("  GET /ping              - Check if server is running"));
		UE_LOG(LogTemp, Log, TEXT("  GET /export?path=...   - Export blueprint to JSON"));
		UE_LOG(LogTemp, Log, TEXT("  GET /list?filter=...   - List available blueprints"));
		UE_LOG(LogTemp, Log, TEXT("  GET /export-struct?path=...   - Export UserDefinedStruct to JSON"));
		UE_LOG(LogTemp, Log, TEXT("  GET /export-enum?path=...    - Export UserDefinedEnum to JSON"));
	}

	virtual void ShutdownModule() override
	{
		FHttpServerModule* HttpServerModule = FModuleManager::Get().GetModulePtr<FHttpServerModule>("HTTPServer");
		if (HttpServerModule)
		{
			HttpServerModule->StopAllListeners();
		}
	}

private:
	TUniquePtr<FHttpServerResponse> MakeJsonResponse(const TSharedPtr<FJsonObject>& JsonObj)
	{
		FString ResponseStr;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseStr);
		FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

		return FHttpServerResponse::Create(ResponseStr, TEXT("application/json"));
	}

	bool HandlePing(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		TSharedPtr<FJsonObject> ResponseObj = MakeShareable(new FJsonObject());
		ResponseObj->SetStringField(TEXT("status"), TEXT("ok"));
		ResponseObj->SetStringField(TEXT("plugin"), TEXT("BlueprintExporter"));
		ResponseObj->SetNumberField(TEXT("port"), BLUEPRINT_EXPORTER_PORT);

		OnComplete(MakeJsonResponse(ResponseObj));
		return true;
	}

	bool HandleExport(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString BlueprintPath;

		const FString* PathParam = Request.QueryParams.Find(TEXT("path"));
		if (PathParam)
		{
			BlueprintPath = *PathParam;
		}

		if (BlueprintPath.IsEmpty())
		{
			TSharedPtr<FJsonObject> ResponseObj = MakeShareable(new FJsonObject());
			ResponseObj->SetBoolField(TEXT("success"), false);
			ResponseObj->SetStringField(TEXT("error"), TEXT("Missing 'path' query parameter. Usage: /export?path=/Game/Path/To/Blueprint"));
			OnComplete(MakeJsonResponse(ResponseObj));
			return true;
		}

		// Ensure path starts with /Game/
		if (!BlueprintPath.StartsWith(TEXT("/Game/")) && !BlueprintPath.StartsWith(TEXT("/Script/")))
		{
			BlueprintPath = TEXT("/Game/") + BlueprintPath;
		}

		// Dispatch to game thread since ExportBlueprintToJson accesses UObjects
		AsyncTask(ENamedThreads::GameThread, [this, BlueprintPath, OnComplete]()
		{
			// Use blueprint name as filename so exports don't overwrite each other
			FString BPName = FPaths::GetBaseFilename(BlueprintPath);
			FString OutputPath = FPaths::Combine(
				FPlatformMisc::GetEnvironmentVariable(TEXT("TEMP")),
				BPName + TEXT(".json")
			);

			bool bSuccess = UBlueprintExporterBPLibrary::ExportBlueprintToJson(BlueprintPath, OutputPath);

			TSharedPtr<FJsonObject> ResponseObj = MakeShareable(new FJsonObject());
			ResponseObj->SetBoolField(TEXT("success"), bSuccess);

			if (bSuccess)
			{
				ResponseObj->SetStringField(TEXT("output_path"), OutputPath);

				int64 FileSize = IFileManager::Get().FileSize(*OutputPath);
				ResponseObj->SetNumberField(TEXT("file_size"), static_cast<double>(FileSize));
			}
			else
			{
				ResponseObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to export blueprint: %s"), *BlueprintPath));
			}

			OnComplete(MakeJsonResponse(ResponseObj));
		});

		return true;
	}

	bool HandleList(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString Filter;

		const FString* FilterParam = Request.QueryParams.Find(TEXT("filter"));
		if (FilterParam)
		{
			Filter = *FilterParam;
		}

		AsyncTask(ENamedThreads::GameThread, [this, Filter, OnComplete]()
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

			TArray<FAssetData> AssetList;
			AssetRegistry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), AssetList);

			TArray<TSharedPtr<FJsonValue>> BlueprintPaths;
			for (const FAssetData& Asset : AssetList)
			{
				FString PackageName = Asset.PackageName.ToString();
				if (Filter.IsEmpty() || PackageName.Contains(Filter))
				{
					BlueprintPaths.Add(MakeShareable(new FJsonValueString(PackageName)));
				}
			}

			TSharedPtr<FJsonObject> ResponseObj = MakeShareable(new FJsonObject());
			ResponseObj->SetBoolField(TEXT("success"), true);
			ResponseObj->SetNumberField(TEXT("count"), BlueprintPaths.Num());
			ResponseObj->SetArrayField(TEXT("blueprints"), BlueprintPaths);

			OnComplete(MakeJsonResponse(ResponseObj));
		});

		return true;
	}

	bool HandleExportStruct(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString StructPath;

		const FString* PathParam = Request.QueryParams.Find(TEXT("path"));
		if (PathParam)
		{
			StructPath = *PathParam;
		}

		if (StructPath.IsEmpty())
		{
			TSharedPtr<FJsonObject> ResponseObj = MakeShareable(new FJsonObject());
			ResponseObj->SetBoolField(TEXT("success"), false);
			ResponseObj->SetStringField(TEXT("error"), TEXT("Missing 'path' query parameter. Usage: /export-struct?path=/Game/Path/To/MyStruct"));
			OnComplete(MakeJsonResponse(ResponseObj));
			return true;
		}

		// Ensure path starts with /Game/
		if (!StructPath.StartsWith(TEXT("/Game/")) && !StructPath.StartsWith(TEXT("/Script/")))
		{
			StructPath = TEXT("/Game/") + StructPath;
		}

		// Dispatch to game thread
		AsyncTask(ENamedThreads::GameThread, [this, StructPath, OnComplete]()
		{
			FString StructName = FPaths::GetBaseFilename(StructPath);
			FString OutputPath = FPaths::Combine(
				FPlatformMisc::GetEnvironmentVariable(TEXT("TEMP")),
				StructName + TEXT("_struct.json")
			);

			bool bSuccess = UBlueprintExporterBPLibrary::ExportStructToJson(StructPath, OutputPath);

			TSharedPtr<FJsonObject> ResponseObj = MakeShareable(new FJsonObject());
			ResponseObj->SetBoolField(TEXT("success"), bSuccess);

			if (bSuccess)
			{
				ResponseObj->SetStringField(TEXT("output_path"), OutputPath);

				int64 FileSize = IFileManager::Get().FileSize(*OutputPath);
				ResponseObj->SetNumberField(TEXT("file_size"), static_cast<double>(FileSize));
			}
			else
			{
				ResponseObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to export struct: %s"), *StructPath));
			}

			OnComplete(MakeJsonResponse(ResponseObj));
		});

		return true;
	}
	bool HandleExportEnum(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		FString EnumPath;

		const FString* PathParam = Request.QueryParams.Find(TEXT("path"));
		if (PathParam)
		{
			EnumPath = *PathParam;
		}

		if (EnumPath.IsEmpty())
		{
			TSharedPtr<FJsonObject> ResponseObj = MakeShareable(new FJsonObject());
			ResponseObj->SetBoolField(TEXT("success"), false);
			ResponseObj->SetStringField(TEXT("error"), TEXT("Missing 'path' query parameter. Usage: /export-enum?path=/Game/Path/To/MyEnum"));
			OnComplete(MakeJsonResponse(ResponseObj));
			return true;
		}

		// Ensure path starts with /Game/
		if (!EnumPath.StartsWith(TEXT("/Game/")) && !EnumPath.StartsWith(TEXT("/Script/")))
		{
			EnumPath = TEXT("/Game/") + EnumPath;
		}

		// Dispatch to game thread
		AsyncTask(ENamedThreads::GameThread, [this, EnumPath, OnComplete]()
		{
			FString EnumName = FPaths::GetBaseFilename(EnumPath);
			FString OutputPath = FPaths::Combine(
				FPlatformMisc::GetEnvironmentVariable(TEXT("TEMP")),
				EnumName + TEXT("_enum.json")
			);

			bool bSuccess = UBlueprintExporterBPLibrary::ExportEnumToJson(EnumPath, OutputPath);

			TSharedPtr<FJsonObject> ResponseObj = MakeShareable(new FJsonObject());
			ResponseObj->SetBoolField(TEXT("success"), bSuccess);

			if (bSuccess)
			{
				ResponseObj->SetStringField(TEXT("output_path"), OutputPath);

				int64 FileSize = IFileManager::Get().FileSize(*OutputPath);
				ResponseObj->SetNumberField(TEXT("file_size"), static_cast<double>(FileSize));
			}
			else
			{
				ResponseObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to export enum: %s"), *EnumPath));
			}

			OnComplete(MakeJsonResponse(ResponseObj));
		});

		return true;
	}
};

IMPLEMENT_MODULE(FBlueprintExporterModule, BlueprintExporter)

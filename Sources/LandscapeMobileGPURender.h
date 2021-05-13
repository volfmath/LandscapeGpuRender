#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "RenderResource.h"
#include "VertexFactory.h"
#include "LandscapeRender.h"
#include "Runtime/Landscape/Private/LandscapePrivate.h"
#include "LandscapeMobileGPURenderEngine.h"
#include "LandscapeGpuRenderProxyComponent.h"

struct FLandscapeClusterVertex;

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLandscapeGpuRenderUniformBuffer, LANDSCAPE_API)
SHADER_PARAMETER(FMatrix, LocalToWorldNoScaling)
SHADER_PARAMETER_TEXTURE(Texture2D, HeightmapTexture)
SHADER_PARAMETER_SAMPLER(SamplerState, HeightmapTextureSampler)
SHADER_PARAMETER_TEXTURE(Texture2D, NormalmapTexture)
SHADER_PARAMETER_SAMPLER(SamplerState, NormalmapTextureSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

//Submit to landscape data
struct FLandscapeSubmitData {
	static FLandscapeSubmitData CreateLandscapeSubmitData(ULandscapeComponent* LandscapeComponent);

	uint32 UniqueWorldId;
	uint32 ClusterSizePerComponent;
	FIntPoint ComponentBase;
	FGuid LandscapeKey;
};

struct FMobileLandscapeGPURenderSystem {
	FMobileLandscapeGPURenderSystem(/*uint32 NumComponents*/);
	~FMobileLandscapeGPURenderSystem();
	static void RegisterGPURenderLandscapeEntity(ULandscapeComponent* InstanceComponent);
	static void UnRegisterGPURenderLandscapeEntity(ULandscapeComponent* InstanceComponent);
	static void RegisterGPURenderLandscapeEntity_RenderThread(FMobileLandscapeGPURenderSystem* RenderSystem, const FLandscapeSubmitData& SubmitToRenderThreadComponentData);
	static void UnRegisterGPURenderLandscapeEntity_RenderThread(const FLandscapeSubmitData& SubmitToRenderThreadComponentData); 
	static TMap<uint32, FMobileLandscapeGPURenderSystem*> LandscapeGPURenderSystem_GameThread;
	static TMap<uint32, FMobileLandscapeGPURenderSystem*> LandscapeGPURenderSystem_RenderThread;

	//[GameThread]
	uint32 NumAllRegisterComponents_GameThread; //the sum of the Entity numbers of all Landscapes, note that System may have multiple Landscapes
	TMap<FGuid, ULandscapeGpuRenderProxyComponent*> LandscapeGpuRenderPeoxyComponens_GameThread; //Resources Manager

	//[RenderThread]
	uint32 NumAllRegisterComponents_RenderThread;
	TMap<FGuid, FLandscapeGpuRenderData> LandscapeGpuRenderDataMap_RenderThread; //A System may have multiple Landscapes
};

//Per ClusterVertexData
struct FLandscapeClusterVertex
{
	uint8 PositionX;
	uint8 PositionY;
	uint8 Blank_0; //Blank Data
	uint8 Blank_1; //Blank Data
};

namespace LandscapeGpuRenderParameter {
	static constexpr uint8 ClusterQuadSize = 16;
	static constexpr uint8 ClusterLod = 5;
	static constexpr uint8 FirstLod = 0;
	static constexpr uint32 ClusterVertexDataSize = ClusterQuadSize * sizeof(FLandscapeClusterVertex);
}

class FLandscapeClusterVertexBuffer : public FVertexBuffer
{
public:
	FLandscapeClusterVertexBuffer();
	virtual ~FLandscapeClusterVertexBuffer();

	/**
	* Initialize the RHI for this rendering resource
	*/
	virtual void InitRHI() override;
};

class FLandscapeGpuRenderProxyComponentSceneProxy final : public FPrimitiveSceneProxy {
public:
	//Mobile Material, Resources Ref
	TArray<UMaterialInterface*> AvailableMaterials;


	SIZE_T GetTypeHash() const override;
	FLandscapeGpuRenderProxyComponentSceneProxy(ULandscapeGpuRenderProxyComponent* InComponent);
	// FPrimitiveSceneProxy interface.
	virtual void ApplyWorldOffset(FVector InOffset) override;
	virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override;
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	virtual int32 CollectOccluderElements(FOccluderElementsCollector& Collector) const override;
	virtual uint32 GetMemoryFootprint() const override { return(sizeof(*this) + GetAllocatedSize()); }
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual bool CanBeOccluded() const override;
	virtual void GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const override;
	virtual void OnTransformChanged() override;
	virtual void CreateRenderThreadResources() override;
	virtual void DestroyRenderThreadResources() override;
	//virtual void OnLevelAddedToWorld() override;
};



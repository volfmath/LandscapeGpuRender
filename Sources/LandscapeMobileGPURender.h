#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "RenderResource.h"
#include "VertexFactory.h"
#include "LandscapeRender.h"
#include "Runtime/Landscape/Private/LandscapePrivate.h"
#include "LandscapeMobileGPURenderEngine.h"


//Submit to landscape daya
struct FLandscapeSubmitData {
	static FLandscapeSubmitData CreateLandscapeSubmitData(ULandscapeComponent* LandscapeComponent);

	uint32 UniqueWorldId;
	uint32 ClusterSizePerComponent;
	FIntPoint ComponentBase;
	FGuid LandscapeKey;
};

struct FMobileLandscapeGPURenderSystem {
	FMobileLandscapeGPURenderSystem();
	~FMobileLandscapeGPURenderSystem();
	static void RegisterGPURenderLandscapeEntity(ULandscapeComponent* InstanceComponent);
	static void UnRegisterGPURenderLandscapeEntity(ULandscapeComponent* InstanceComponent);
	static void RegisterGPURenderLandscapeEntity_RenderThread(FMobileLandscapeGPURenderSystem* RenderSystem, const FLandscapeSubmitData& SubmitToRenderThreadComponentData);
	static void UnRegisterGPURenderLandscapeEntity_RenderThread(const FLandscapeSubmitData& SubmitToRenderThreadComponentData);
	static TMap<uint32, FMobileLandscapeGPURenderSystem*> LandscapeGPURenderSystem_GameThread;
	static TMap<uint32, FMobileLandscapeGPURenderSystem*> LandscapeGPURenderSystem_RenderThread;

	uint32 NumRegisterLandscape_GameThread;
	uint32 NumRegisterLandscape_RenderThread;
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

class FLandscapeClusterVertexBuffer : public FVertexBuffer
{
public:
	static constexpr uint32 ClusterQuadSize = 16;
	static constexpr uint32 ClusterLod = 5;
	static constexpr uint32 ClusterVertexDataSize = ClusterQuadSize * sizeof(FLandscapeClusterVertex);

	FLandscapeClusterVertexBuffer();
	virtual ~FLandscapeClusterVertexBuffer();

	/**
	* Initialize the RHI for this rendering resource
	*/
	virtual void InitRHI() override;
};



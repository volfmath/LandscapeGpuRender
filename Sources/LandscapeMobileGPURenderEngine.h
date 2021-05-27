#pragma once
#include "CoreMinimal.h"
#include "RHIUtilities.h"

extern ENGINE_API TAutoConsoleVariable<int32> CVarMobileLandscapeGpuRender;

struct FLandscapeSubmitData;

//Per ClusterVertexData
struct FLandscapeClusterVertex
{
	//uint8 PositionX;
	//uint8 PositionY;
	//uint8 Blank_0; //Blank Data
	//uint8 Blank_1; //Blank Data

	float PositionX;
	float PositionY;
};

namespace LandscapeGpuRenderParameter {
	static constexpr uint8 ClusterQuadSize = 16;
	static constexpr uint8 ClusterLod = 5;
	static constexpr uint8 FirstLod = 0;
	static constexpr uint32 ClusterVertexDataSize = ClusterQuadSize * sizeof(FLandscapeClusterVertex);
}

struct FLandscapeGpuRenderUserData {
	FRHIUniformBuffer* LandscapeGpuRenderUniformBuffer;
	FRHIShaderResourceView* LandscapeGpuRenderOutputBufferSRV;
};

//Because cs write, cache friend 
struct FLandscapeClusterLODData_CPU {
	uint32 ClusterLOD;
};

//
struct FLandscapeClusterInputData_CPU {
	FVector BoundCenter;
	uint32 ClusterIndexX : 8; //0~255
	uint32 ClusterIndexY : 8; //0~255
	uint32 ComponentIndexX : 8;
	uint32 ComponentIndexY : 8;
	FVector BoundExtent;
};

//HUAWEI Error?
struct FLandscapeClusterOutputData_CPU {
	uint32 ClusterIndexX : 8; //0~255, 
	uint32 ClusterIndexY : 8; //0~255
	uint32 DownLod : 3;
	uint32 LeftLod : 3;
	uint32 TopLod : 3;
	uint32 RightLod : 3;
	uint32 CenterLod : 3;
};

struct FLandscapeGpuRenderProxyComponent_RenderThread {
	FLandscapeGpuRenderProxyComponent_RenderThread();
	~FLandscapeGpuRenderProxyComponent_RenderThread();

	ENGINE_API void UpdateAllGPUBuffer();
	ENGINE_API void InitClusterData(const TArray<FBox>& ClusterBoundingArray, const FMatrix& LocalToWorldMatrix);
	void RegisterComponentData(const FLandscapeSubmitData& SubmitToRenderThreadComponentData);
	void UnRegisterComponentData();
	void MarkDirty();
	inline uint32 GetLinearIndexByClusterIndex(const FIntPoint& ClusterIndex) const;

	bool bLandscapeDirty;

	//Just Write once
	uint32 NumSections;
	uint32 ClusterSizePerSection;
	FVector4 LodSettingParameters;

	//Write multiple times
	uint32 NumRegisterComponent;
	FIntPoint LandscapeComponentMin;
	FIntPoint LandscapeComponentSize;

	//[Resources Ref]
	FLandscapeGpuRenderUserData LandscapeGpuRenderUserData;

	//[Resources Manager Auto Release]
	TArray<FBoxSphereBounds> WorldClusterBounds;

	//[Resources Manager Auto Release]
	TArray<FVector4> ComponentsOriginAndRadius;

	//[Resources Manager]
	FRWBuffer LandscapeClusterLODData_GPU;
	FReadBuffer ComponentOriginAndRadius_GPU;

	FRWBuffer IndirectDrawCommandBuffer_GPU;
	FRWBufferStructured ClusterInputData_GPU;
	FRWBuffer ClusterOutputData_GPU;
};

/**
 * Landscape Data
 */
struct FMobileLandscapeGPURenderSystem_RenderThread {
	FMobileLandscapeGPURenderSystem_RenderThread();
	~FMobileLandscapeGPURenderSystem_RenderThread();

	static TMap<uint32, FMobileLandscapeGPURenderSystem_RenderThread*> LandscapeGPURenderSystem_RenderThread;
	ENGINE_API static void RegisterGPURenderLandscapeEntity_RenderThread(const FLandscapeSubmitData& SubmitToRenderThreadComponentData);
	ENGINE_API static void UnRegisterGPURenderLandscapeEntity_RenderThread(const FLandscapeSubmitData& SubmitToRenderThreadComponentData);
	ENGINE_API static FMobileLandscapeGPURenderSystem_RenderThread* GetLandscapeGPURenderSystem_RenderThread(const uint32 UniqueWorldId);
	ENGINE_API static FLandscapeGpuRenderProxyComponent_RenderThread& GetLandscapeGPURenderComponent_RenderThread(const uint32 UniqueWorldId, const FGuid& LandscapeKey);

	//[RenderThread]
	uint32 NumAllRegisterComponents_RenderThread;
	TMap<FGuid, FLandscapeGpuRenderProxyComponent_RenderThread> LandscapeGpuRenderComponent_RenderThread; //A System may have multiple Landscapes
};



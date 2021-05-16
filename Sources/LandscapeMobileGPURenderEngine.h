#pragma once
#include "CoreMinimal.h"
#include "RHIUtilities.h"

extern ENGINE_API TAutoConsoleVariable<int32> CVarMobileLandscapeGpuRender;

struct FLandscapeSubmitData;

//压缩数据,天刀使用离线高度计算Bounding
struct FLandscapeClusterInputData_CPU {
	FVector BoundCenter;
	uint32 ClusterIndexX : 8; //0~127
	uint32 ClusterIndexY : 8; //0~127
	uint32 ComponentIndexX : 8;
	uint32 ComponentIndexY : 8;
	FVector BoundExtent;
};

struct FLandscapeClusterOutputData_CPU {
	uint16 ClusterIndexX : 8; //0~127
	uint16 ClusterIndexY : 8; //0~127
	//uint32 ComponentIndexX : 8;
	//uint32 ComponentIndexY : 8;
};

struct FLandscapeGpuRenderProxyComponent_RenderThread {
	FLandscapeGpuRenderProxyComponent_RenderThread();
	~FLandscapeGpuRenderProxyComponent_RenderThread();

	ENGINE_API void UpdateAllGPUBuffer();
	void RegisterComponentData(const FLandscapeSubmitData& SubmitToRenderThreadComponentData);
	void UnRegisterComponentData();
	void MarkDirty();

	bool bLandscapeDirty;
	uint32 NumRegisterComponent;
	uint32 ClusterSizePerComponent;
	FIntPoint LandscapeComponentMin;
	FIntPoint LandscapeComponentSize;

	//[Resources Manager]
	FRWBuffer IndirectDrawCommandBuffer_GPU;
	FRWBufferStructured ClusterInputData_GPU;
	FRWBuffer ClusterOutputData_GPU;
};

/**
 * Landscape Data
 * 不论地块形状如何, 容器大小是基本固定的, 只是内部是否存在数据, 这样是为了正确计算LOD
 */
struct FMobileLandscapeGPURenderSystem_RenderThread {
	FMobileLandscapeGPURenderSystem_RenderThread();
	~FMobileLandscapeGPURenderSystem_RenderThread();

	static TMap<uint32, FMobileLandscapeGPURenderSystem_RenderThread*> LandscapeGPURenderSystem_RenderThread;
	ENGINE_API static void RegisterGPURenderLandscapeEntity_RenderThread(const FLandscapeSubmitData& SubmitToRenderThreadComponentData);
	ENGINE_API static void UnRegisterGPURenderLandscapeEntity_RenderThread(const FLandscapeSubmitData& SubmitToRenderThreadComponentData);
	ENGINE_API static FMobileLandscapeGPURenderSystem_RenderThread* GetLandscapeGPURenderSystem_RenderThread(const uint32 UniqueWorldId);
	ENGINE_API static const FLandscapeGpuRenderProxyComponent_RenderThread& GetLandscapeGPURenderComponent_RenderThread(const uint32 UniqueWorldId, const FGuid& LandscapeKey);

	//[RenderThread]
	uint32 NumAllRegisterComponents_RenderThread;
	TMap<FGuid, FLandscapeGpuRenderProxyComponent_RenderThread> LandscapeGpuRenderComponent_RenderThread; //A System may have multiple Landscapes
};



#pragma once
#include "CoreMinimal.h"
#include "RHIUtilities.h"

extern ENGINE_API TAutoConsoleVariable<int32> CVarMobileLandscapeGpuRender;

struct FLandscapeSubmitData;

struct FLandscapeGpuRenderUserData {
	FRHIUniformBuffer* LandscapeGpuRenderUniformBuffer;
	FRHIShaderResourceView* LandscapeGpuRenderOutputBufferSRV;
};

//压缩数据,天刀使用离线高度计算Bounding
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
	uint32 ClusterIndexX : 8; //0~255
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
	void RegisterComponentData(const FLandscapeSubmitData& SubmitToRenderThreadComponentData);
	void UnRegisterComponentData();
	void MarkDirty();
	inline uint32 GetLinearIndexByClusterIndex(const FIntPoint& ClusterIndex) const;

	bool bLandscapeDirty;
	uint32 NumRegisterComponent;
	uint32 NumSections;
	uint32 ClusterSizePerSection;
	FIntPoint LandscapeComponentMin;
	FIntPoint LandscapeComponentSize;

	//[Resources Manager]
	FLandscapeGpuRenderUserData LandscapeGpuRenderUserData;
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



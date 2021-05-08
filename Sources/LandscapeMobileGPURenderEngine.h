#pragma once
#include "CoreMinimal.h"
#include "RHIUtilities.h"

struct FLandscapeSubmitData;

struct FDrawIndirectCommandArgs_CPU {
	uint32    IndexCount;
	uint32    InstanceCount;
	uint32    FirstIndex;
	int32     VertexOffset;
	uint32    FirstInstance;
};

//压缩数据,天刀使用离线高度计算Bounding
struct FLandscapeClusterInputData_CPU {
	FVector BoundCenter;
	uint32 ClusterIndexX : 8; //0~127
	uint32 ClusterIndexY : 8; //0~127
	uint32 ComponentIndexX : 8;
	uint32 ComponentIndexY : 8;
	FVector BoundExtent;
};

struct FLandscapeClusterOutputData_GPU {
	uint16 ClusterIndexX : 8; //0~127
	uint16 ClusterIndexY : 8; //0~127
	//uint32 ComponentIndexX : 8;
	//uint32 ComponentIndexY : 8;
};

/**
 * Landscape Data
 * 不论地块形状如何, 容器大小是基本固定的, 只是内部是否存在数据, 这样是为了正确计算LOD
 */
struct FLandscapeGpuRenderData {
	ENGINE_API FLandscapeGpuRenderData();
	ENGINE_API ~FLandscapeGpuRenderData();

	ENGINE_API void UpdateAllGPUBuffer();
	ENGINE_API void RegisterComponent(const FLandscapeSubmitData& SubmitToRenderThreadComponentData);
	ENGINE_API void UnRegisterComponent();
	ENGINE_API void MarkDirty();

	bool bLandscapeDirty;
	uint32 NumRegisterComponent;
	uint32 ClusterSizePerComponent;
	FIntPoint LandscapeComponentMin;
	FIntPoint LandscapeComponentSize;

	FRWBuffer IndirectDrawCommandBuffer_GPU;
	FRWBufferStructured ClusterInputData_GPU;
	FRWBuffer ClusterOutputData_GPU;


};

#include "LandscapeMobileGPURenderEngine.h"
#include "LandscapeMobileGPURender.h"

ENGINE_API TAutoConsoleVariable<int32> CVarMobileLandscapeGpuRender(
	TEXT("r.GpuDriven.LandscapeGpuRender"),
	1,
	TEXT("Enable GpuRender For Landscape"),
	ECVF_Scalability
);


FLandscapeGpuRenderData::FLandscapeGpuRenderData() 
	: bLandscapeDirty(false)
	, NumRegisterComponent(0)
	, ClusterSizePerComponent(0)
	, LandscapeComponentMin(INT32_MAX, INT32_MAX)
	, LandscapeComponentSize(FIntPoint(0,0))
{

}

FLandscapeGpuRenderData::~FLandscapeGpuRenderData() {
	IndirectDrawCommandBuffer_GPU.Release();
	ClusterInputData_GPU.Release();
	ClusterOutputData_GPU.Release();
}

void FLandscapeGpuRenderData::UpdateAllGPUBuffer() {
	check(IsInRenderingThread());
	check(LandscapeComponentMin.X == 0 && LandscapeComponentMin.Y == 0);

	if (bLandscapeDirty && NumRegisterComponent != 0) {
		//Release Resources
		IndirectDrawCommandBuffer_GPU.Release();
		ClusterInputData_GPU.Release();
		ClusterOutputData_GPU.Release();

		//IndirectDrawBuffer
		TArray<FDrawIndirectCommandArgs_CPU> IndirectDrawCommandBuffer_CPU;
		IndirectDrawCommandBuffer_CPU.AddZeroed(/*FLandscapeClusterVertexBuffer::ClusterLod*/1);
		for (int32 DrawElementIndex = 0; DrawElementIndex < IndirectDrawCommandBuffer_CPU.Num(); ++DrawElementIndex) {
			auto& DrawCommandBuffer = IndirectDrawCommandBuffer_CPU[DrawElementIndex];
			DrawCommandBuffer.IndexCount = LandscapeGpuRenderParameter::ClusterQuadSize * LandscapeGpuRenderParameter::ClusterQuadSize * 2 * 3;
			DrawCommandBuffer.InstanceCount = ClusterSizePerComponent * ClusterSizePerComponent * NumRegisterComponent;
			DrawCommandBuffer.FirstIndex = 0;
			DrawCommandBuffer.VertexOffset = 0;
			DrawCommandBuffer.FirstInstance = 0;
		}


		//ClusterOutoutBuffer, 临时写入out代码, 直接传给VS
		TArray<FLandscapeClusterOutputData_GPU> ClusterOutputData_CPU;
		//TArray<FLandscapeClusterInputData_CPU> CluterInputData_CPU;
		ClusterOutputData_CPU.AddZeroed(ClusterSizePerComponent * ClusterSizePerComponent * LandscapeComponentSize.X * LandscapeComponentSize.Y);
		//CluterInputData_CPU.AddZeroed(ClusterSizePerComponent * ClusterSizePerComponent * ClusterSize.X * ClusterSize.Y);
		for (int32 ClusterIndexY = 0; ClusterIndexY < LandscapeComponentSize.Y; ++ClusterIndexY) {
			for (int32 ClusterIndexX = 0; ClusterIndexX < LandscapeComponentSize.X; ++ClusterIndexX) {
				int32 CalculateIndex = ClusterIndexX + ClusterIndexY * ClusterSizePerComponent;
				ClusterOutputData_CPU[CalculateIndex].ClusterIndexX = (ClusterIndexX & 0xFF);
				ClusterOutputData_CPU[CalculateIndex].ClusterIndexY = (ClusterIndexY & 0xFF);
			}
		}
	}
}

void FLandscapeGpuRenderData::RegisterComponent(const FLandscapeSubmitData& SubmitToRenderThreadComponentData) {
	const FIntPoint& ComponentBase = SubmitToRenderThreadComponentData.ComponentBase;
	if (ClusterSizePerComponent == 0) {
		ClusterSizePerComponent = SubmitToRenderThreadComponentData.ClusterSizePerComponent;
	}
	else {
		check(ClusterSizePerComponent == SubmitToRenderThreadComponentData.ClusterSizePerComponent); //每个component一定相等?
	}
	if (NumRegisterComponent > 0) {
		FIntPoint OriginalMin = LandscapeComponentMin;
		FIntPoint OriginalMax = LandscapeComponentMin + LandscapeComponentSize - FIntPoint(1, 1);
		FIntPoint NewMin(FMath::Min(OriginalMin.X, ComponentBase.X), FMath::Min(OriginalMin.Y, ComponentBase.Y));
		FIntPoint NewMax(FMath::Max(OriginalMax.X, ComponentBase.X), FMath::Max(OriginalMax.Y, ComponentBase.Y));
		FIntPoint SizeRequired = (NewMax - NewMin) + FIntPoint(1, 1);
		LandscapeComponentMin = NewMin;
		LandscapeComponentSize = SizeRequired;
	}
	else {
		LandscapeComponentMin = ComponentBase;
		LandscapeComponentSize = FIntPoint(1, 1);
	}
	NumRegisterComponent += 1;
	MarkDirty();
}

void FLandscapeGpuRenderData::UnRegisterComponent() {
	NumRegisterComponent -= 1;
	MarkDirty();
}

void FLandscapeGpuRenderData::MarkDirty() {
	bLandscapeDirty = true;
}
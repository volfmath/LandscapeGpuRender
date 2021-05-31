#include "LandscapeMobileGPURenderEngine.h"
#include "LandscapeMobileGPURender.h"
#include "MobileGpuDriven.h"

ENGINE_API TAutoConsoleVariable<int32> CVarMobileLandscapeGpuRender(
	TEXT("r.GpuDriven.LandscapeGpuRender"),
	1,
	TEXT("Enable GpuRender For Landscape"),
	ECVF_Scalability
);

ENGINE_API TAutoConsoleVariable<int32> CVarMobileComputeShaderControl(
	TEXT("r.GpuDriven.LandscapeComputeShader"),
	1,
	TEXT("Enable GpuRender For Landscape"),
	ECVF_Scalability
);


FLandscapeGpuRenderProxyComponent_RenderThread::FLandscapeGpuRenderProxyComponent_RenderThread()
	: bLandscapeDirty(false)
	, NumSections(0)
	, ClusterSizePerSection(0)
	, ClusterSizeX(0)
	, ClusterSizeY(0)
	, LodSettingParameters(EForceInit::ForceInitToZero)
	, NumRegisterComponent(0)
	, LandscapeComponentMin(INT32_MAX, INT32_MAX)
	, LandscapeComponentSize(FIntPoint(0,0))
{

}

FLandscapeGpuRenderProxyComponent_RenderThread::~FLandscapeGpuRenderProxyComponent_RenderThread() {
	check(NumRegisterComponent == 0);
	LandscapeClusterLODData_GPU.Release();
	ComponentOriginAndRadius_GPU.Release();
	ClusterInputData_GPU.Release();
	ClusterOutputData_GPU.Release();
	ClusterLodCountUAV_GPU.Release();
	OrderClusterOutBufferUAV_GPU.Release();
	IndirectDrawCommandBuffer_GPU.Release();
}

uint32 FLandscapeGpuRenderProxyComponent_RenderThread::GetLinearIndexByClusterIndex(const FIntPoint& ClusterIndex) const {
	const uint32 ClusterSizePerComponent = ClusterSizePerSection * NumSections;
	const uint32 ClusterSqureSizePerComponent = ClusterSizePerComponent * ClusterSizePerComponent;

	FIntPoint ClampSize = FIntPoint(
		FMath::Clamp(ClusterIndex.X, 0, static_cast<int32>(LandscapeComponentSize.X * ClusterSizePerComponent - 1)),
		FMath::Clamp(ClusterIndex.Y, 0, static_cast<int32>(LandscapeComponentSize.Y * ClusterSizePerComponent - 1))
	);

	const uint32 ClusterOffsetX = ClampSize.X & (ClusterSizePerComponent - 1);
	const uint32 ClusterOffsetY = ClampSize.Y & (ClusterSizePerComponent - 1);
	const uint32 ComponentOffsetX = ClampSize.X / ClusterSizePerComponent;
	const uint32 ComponentOffsetY = ClampSize.Y / ClusterSizePerComponent;
	const uint32 Offset_1 = ComponentOffsetY * ClusterSqureSizePerComponent * LandscapeComponentSize.X + ComponentOffsetX * ClusterSqureSizePerComponent;
	const uint32 Offset_2 = ClusterOffsetX + ClusterOffsetY * ClusterSizePerComponent;
	return Offset_1 + Offset_2;
}

void FLandscapeGpuRenderProxyComponent_RenderThread::InitClusterData(const TArray<FBox>& ClusterBoundingArray, const FMatrix& LocalToWorldMatrix) {
	check(IsInRenderingThread());
	WorldClusterBounds.SetNumZeroed(ClusterBoundingArray.Num());
	for (int32 Index = 0; Index < ClusterBoundingArray.Num(); ++Index) {
		WorldClusterBounds[Index] = FBoxSphereBounds(ClusterBoundingArray[Index]).TransformBy(LocalToWorldMatrix);
	}

	//Component的位置为所有Bounding叠加在一起的中心位置
	const uint32 ClusterSizePerComponent = ClusterSizePerSection * NumSections;
	const uint32 ClusterSqureSizePerComponent = ClusterSizePerComponent * ClusterSizePerComponent;
	for (int32 ComponentY = 0; ComponentY < LandscapeComponentSize.Y; ++ComponentY) {
		for (int32 ComponentX = 0; ComponentX < LandscapeComponentSize.X; ++ComponentX) {
			uint32 StartIndex = (ComponentX + ComponentY * LandscapeComponentSize.X) * ClusterSqureSizePerComponent;
			FBox ComponetnBoxds = FBox(EForceInit::ForceInit);
			for (uint32 LinearIndex = 0; LinearIndex < ClusterSqureSizePerComponent; ++LinearIndex) {
				ComponetnBoxds += WorldClusterBounds[StartIndex + LinearIndex].GetBox();
			}
			FBoxSphereBounds SphereBound = FBoxSphereBounds(ComponetnBoxds);
			ComponentsOriginAndRadius.Emplace(FVector4(SphereBound.Origin, SphereBound.SphereRadius));
		}
	}
}

void FLandscapeGpuRenderProxyComponent_RenderThread::UpdateAllGPUBuffer() {
	if (bLandscapeDirty && NumRegisterComponent != 0) {
		check(IsInRenderingThread());
		check(LandscapeComponentMin.X == 0 && LandscapeComponentMin.Y == 0);
		check(NumRegisterComponent == LandscapeComponentSize.X * LandscapeComponentSize.Y);
		uint32 ClusterSizePerComponent = ClusterSizePerSection * NumSections;
		uint32 ClusterSqureSizePerComponent = ClusterSizePerComponent * ClusterSizePerComponent;
		ClusterSizeX = ClusterSizePerSection * NumSections * LandscapeComponentSize.X;
		ClusterSizeY = ClusterSizePerSection * NumSections * LandscapeComponentSize.Y;

		check(ClusterSqureSizePerComponent * NumRegisterComponent < 0x10000); //Make sure 
		
		//Release Resources
		LandscapeClusterLODData_GPU.Release();
		ComponentOriginAndRadius_GPU.Release();
		ClusterInputData_GPU.Release();
		ClusterOutputData_GPU.Release();
		ClusterLodCountUAV_GPU.Release();
		OrderClusterOutBufferUAV_GPU.Release();
		IndirectDrawCommandBuffer_GPU.Release();
		
		//IndirectDrawBuffer
		TArray<FLandscapeClusterInputData_CPU> ClusterInputData_CPU;
		ClusterInputData_CPU.AddZeroed(ClusterSqureSizePerComponent * NumRegisterComponent);
		for (int32 ComponentIndexY = 0; ComponentIndexY < LandscapeComponentSize.Y; ++ComponentIndexY) {
			for (int32 ComponentIndexX = 0; ComponentIndexX < LandscapeComponentSize.X; ++ComponentIndexX) {
				for (uint32 LocalClusterIndexY = 0; LocalClusterIndexY < ClusterSizePerComponent; ++LocalClusterIndexY) {
					for (uint32 LocalClusterIndexX = 0; LocalClusterIndexX < ClusterSizePerComponent; ++LocalClusterIndexX) {
						FIntPoint GlobalClusterIndex = FIntPoint(LocalClusterIndexX + ComponentIndexX * ClusterSizePerComponent, LocalClusterIndexY + ComponentIndexY * ClusterSizePerComponent);
						uint32 ClusterIndex = GetLinearIndexByClusterIndex(GlobalClusterIndex);
						ClusterInputData_CPU[ClusterIndex].BoundCenter = WorldClusterBounds[ClusterIndex].Origin;
						ClusterInputData_CPU[ClusterIndex].Pad_0 = 0.f;
						ClusterInputData_CPU[ClusterIndex].BoundExtent = WorldClusterBounds[ClusterIndex].BoxExtent;
						ClusterInputData_CPU[ClusterIndex].Pad_1 = 0.f;
					}
				}
			}
		}

		TArray<FDrawIndirectCommandArgs_CPU> IndirectDrawCommandBuffer_CPU;
		IndirectDrawCommandBuffer_CPU.AddZeroed(LandscapeGpuRenderParameter::ClusterLodCount);
		for (int32 DrawElementIndex = 0; DrawElementIndex < IndirectDrawCommandBuffer_CPU.Num(); ++DrawElementIndex) {
			int32 LodClusterQuadSize = LandscapeGpuRenderParameter::ClusterQuadSize >> DrawElementIndex;
			auto& DrawCommandBuffer = IndirectDrawCommandBuffer_CPU[DrawElementIndex];
			DrawCommandBuffer.IndexCount = LodClusterQuadSize * LodClusterQuadSize * 2 * 3;
			DrawCommandBuffer.InstanceCount = 0;
			DrawCommandBuffer.FirstIndex = 0;
			DrawCommandBuffer.VertexOffset = 0;
			DrawCommandBuffer.FirstInstance = 0;
		}

		//ComponentData
		ComponentOriginAndRadius_GPU.Initialize(sizeof(FVector4), ComponentsOriginAndRadius.Num(), PF_A32B32G32R32F, BUF_Static);
		void* ComponentDataPtr = RHILockVertexBuffer(ComponentOriginAndRadius_GPU.Buffer, 0, ComponentOriginAndRadius_GPU.NumBytes, RLM_WriteOnly);
		FMemory::Memcpy(ComponentDataPtr, ComponentsOriginAndRadius.GetData(), ComponentOriginAndRadius_GPU.NumBytes);
		RHIUnlockVertexBuffer(ComponentOriginAndRadius_GPU.Buffer);

		//InputData
		ClusterInputData_GPU.Initialize(sizeof(FLandscapeClusterInputData_CPU), ClusterInputData_CPU.Num(), BUF_Static);
		void* MappingAndBoundData = RHILockStructuredBuffer(ClusterInputData_GPU.Buffer, 0, ClusterInputData_GPU.NumBytes, RLM_WriteOnly);
		FMemory::Memcpy(MappingAndBoundData, ClusterInputData_CPU.GetData(), ClusterInputData_GPU.NumBytes);
		RHIUnlockStructuredBuffer(ClusterInputData_GPU.Buffer);

		//LodDataBuffer
		LandscapeClusterLODData_GPU.Initialize(sizeof(FLandscapeClusterLODData_CPU), ClusterSqureSizePerComponent * NumRegisterComponent, PF_R32_UINT, BUF_Static);

		//OutputData
		check(ClusterSqureSizePerComponent * NumRegisterComponent < 65536);
		ClusterOutputData_GPU.Initialize(sizeof(uint32), ClusterSqureSizePerComponent * NumRegisterComponent * 2, PF_R32_UINT, BUF_Static);

		//LodCountData
		ClusterLodCountUAV_GPU.Initialize(sizeof(uint32), LandscapeGpuRenderParameter::ClusterLodCount, PF_R32_UINT, BUF_Static);

		//OrderOutputData
		OrderClusterOutBufferUAV_GPU.Initialize(sizeof(FLandscapeClusterPackData_CPU), ClusterSqureSizePerComponent * NumRegisterComponent, PF_R32_UINT, BUF_Static);

		//IndirectDrawData
		IndirectDrawCommandBuffer_GPU.Initialize(sizeof(uint32), IndirectDrawCommandBuffer_CPU.Num() * SLGPUDrivenParameter::IndirectBufferElementSize, PF_R32_UINT, BUF_DrawIndirect | BUF_Static);
		void* IndirectBufferData = RHILockVertexBuffer(IndirectDrawCommandBuffer_GPU.Buffer, 0, IndirectDrawCommandBuffer_GPU.NumBytes, RLM_WriteOnly);
		FMemory::Memcpy(IndirectBufferData, IndirectDrawCommandBuffer_CPU.GetData(), IndirectDrawCommandBuffer_GPU.NumBytes);
		RHIUnlockVertexBuffer(IndirectDrawCommandBuffer_GPU.Buffer);

		//UserData
		LandscapeGpuRenderUserData.LandscapeGpuRenderOutputBufferSRV = OrderClusterOutBufferUAV_GPU.SRV;
		LandscapeGpuRenderUserData.LandscapeGpuRenderFirstIndexSRV = ClusterLodCountUAV_GPU.SRV;

		bLandscapeDirty = false;
	}
}

void FLandscapeGpuRenderProxyComponent_RenderThread::RegisterComponentData(const FLandscapeSubmitData& SubmitToRenderThreadComponentData) {
	const FIntPoint& ComponentBase = SubmitToRenderThreadComponentData.ComponentBase;
	if (ClusterSizePerSection == 0) {
		NumSections = SubmitToRenderThreadComponentData.NumSections;
		ClusterSizePerSection = SubmitToRenderThreadComponentData.ClusterSizePerSection;
		LodSettingParameters = SubmitToRenderThreadComponentData.LodSettingParameters;
	}
	else {
		check(ClusterSizePerSection == SubmitToRenderThreadComponentData.ClusterSizePerSection); //每个component一定相等
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

void FLandscapeGpuRenderProxyComponent_RenderThread::UnRegisterComponentData() {
	NumRegisterComponent -= 1;
	MarkDirty();
}

void FLandscapeGpuRenderProxyComponent_RenderThread::MarkDirty() {
	bLandscapeDirty = true;
}

//------------------------------------------------SystemRenderThread------------------------------------------------//
TMap<uint32, FMobileLandscapeGPURenderSystem_RenderThread*> FMobileLandscapeGPURenderSystem_RenderThread::LandscapeGPURenderSystem_RenderThread;

FMobileLandscapeGPURenderSystem_RenderThread::FMobileLandscapeGPURenderSystem_RenderThread(/*uint32 NumComponents*/)
//: NumComponents(0)
	: NumAllRegisterComponents_RenderThread(0)
{

}

FMobileLandscapeGPURenderSystem_RenderThread::~FMobileLandscapeGPURenderSystem_RenderThread() {
	check(NumAllRegisterComponents_RenderThread == 0);
}

void FMobileLandscapeGPURenderSystem_RenderThread::RegisterGPURenderLandscapeEntity_RenderThread(const FLandscapeSubmitData& SubmitToRenderThreadComponentData) {
	check(IsInRenderingThread());

	FMobileLandscapeGPURenderSystem_RenderThread** FoundSystemPtr = LandscapeGPURenderSystem_RenderThread.Find(SubmitToRenderThreadComponentData.UniqueWorldId);
	FMobileLandscapeGPURenderSystem_RenderThread* FoundSystem = FoundSystemPtr == nullptr
		? LandscapeGPURenderSystem_RenderThread.Emplace(SubmitToRenderThreadComponentData.UniqueWorldId, new FMobileLandscapeGPURenderSystem_RenderThread())
		: *FoundSystemPtr;

	FLandscapeGpuRenderProxyComponent_RenderThread* RenderComponent = FoundSystem->LandscapeGpuRenderComponent_RenderThread.Find(SubmitToRenderThreadComponentData.LandscapeKey);
	if (RenderComponent == nullptr) {
		FLandscapeGpuRenderProxyComponent_RenderThread& EmplaceComponent = FoundSystem->LandscapeGpuRenderComponent_RenderThread.Emplace(
			SubmitToRenderThreadComponentData.LandscapeKey, 
			FLandscapeGpuRenderProxyComponent_RenderThread()
		);
		RenderComponent = &EmplaceComponent;
	}
	RenderComponent->RegisterComponentData(SubmitToRenderThreadComponentData);
	FoundSystem->NumAllRegisterComponents_RenderThread += 1;
}

void FMobileLandscapeGPURenderSystem_RenderThread::UnRegisterGPURenderLandscapeEntity_RenderThread(const FLandscapeSubmitData& SubmitToRenderThreadComponentData) {
	check(IsInRenderingThread());
	FMobileLandscapeGPURenderSystem_RenderThread* FoundSystem = LandscapeGPURenderSystem_RenderThread.FindChecked(SubmitToRenderThreadComponentData.UniqueWorldId);
	FLandscapeGpuRenderProxyComponent_RenderThread& RenderComponent = FoundSystem->LandscapeGpuRenderComponent_RenderThread.FindChecked(SubmitToRenderThreadComponentData.LandscapeKey);

	//Release Component
	RenderComponent.UnRegisterComponentData();
	if (RenderComponent.NumRegisterComponent == 0) {
		FoundSystem->LandscapeGpuRenderComponent_RenderThread.Remove(SubmitToRenderThreadComponentData.LandscapeKey);
	}

	//Release System
	FoundSystem->NumAllRegisterComponents_RenderThread -= 1;
	if (FoundSystem->NumAllRegisterComponents_RenderThread == 0) {
		LandscapeGPURenderSystem_RenderThread.Remove(SubmitToRenderThreadComponentData.UniqueWorldId); //End of life of the system container
		delete FoundSystem;
	}
}

FMobileLandscapeGPURenderSystem_RenderThread* FMobileLandscapeGPURenderSystem_RenderThread::GetLandscapeGPURenderSystem_RenderThread(const uint32 UniqueWorldId) {
	FMobileLandscapeGPURenderSystem_RenderThread** FoundSystem = LandscapeGPURenderSystem_RenderThread.Find(UniqueWorldId);
	if (FoundSystem) {
		return *FoundSystem;
	}
	else {
		return nullptr;
	}
}

FLandscapeGpuRenderProxyComponent_RenderThread& FMobileLandscapeGPURenderSystem_RenderThread::GetLandscapeGPURenderComponent_RenderThread(const uint32 UniqueWorldId, const FGuid& LandscapeKey) {
	FMobileLandscapeGPURenderSystem_RenderThread* FoundSystem = LandscapeGPURenderSystem_RenderThread.FindChecked(UniqueWorldId);
	FLandscapeGpuRenderProxyComponent_RenderThread& FoundRenderData = FoundSystem->LandscapeGpuRenderComponent_RenderThread.FindChecked(LandscapeKey);
	return FoundRenderData;
}

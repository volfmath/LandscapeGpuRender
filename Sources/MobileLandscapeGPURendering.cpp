#include "MobileLandscapeGPURendering.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "MobileHZB.h"
#include "LandscapeMobileGPURenderEngine.h"

constexpr uint32 ThreadCount = 64;
constexpr uint32 ThreadCount_1 = 8;

class FComputeLandscapeLodCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComputeLandscapeLodCS);

public:
	FComputeLandscapeLodCS() : FGlobalShader() {}

	FComputeLandscapeLodCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer) 
	{
		LodCSParameters.Bind(Initializer.ParameterMap, TEXT("LodCSParameters"));
		ComponentsOriginAndRadiusSRV.Bind(Initializer.ParameterMap, TEXT("ComponentsOriginAndRadiusSRV"));
		ClusterLodBufferUAV.Bind(Initializer.ParameterMap, TEXT("ClusterLodBufferUAV"));
		ClusterLodCountUAV_0.Bind(Initializer.ParameterMap, TEXT("ClusterLodCountUAV_0"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) {
		return true;
	}

	void BindParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FLandscapeGpuRenderProxyComponent_RenderThread& RenderComponentData) {

		//TArray<FVector4, TInlineAllocator<4>> 
		//See detailed definition in shader
		FVector4 PackConstBufferData[3];
		constexpr auto PackConstBufferSize = UE_ARRAY_COUNT(PackConstBufferData);
		const auto& ProjMatrix = View.ViewMatrices.GetProjectionMatrix();
		const float ClusterSqureSizePerComponent = FMath::Square(RenderComponentData.NumSections * RenderComponentData.ClusterSizePerSection);
		PackConstBufferData[0] = FVector4(View.ViewMatrices.GetViewOrigin(), 0.f);
		PackConstBufferData[1] = FVector4( ProjMatrix.M[0][0], ProjMatrix.M[1][1], ProjMatrix.M[2][3], ClusterSqureSizePerComponent);
		PackConstBufferData[2] = RenderComponentData.LodSettingParameters;
		SetShaderValueArray(RHICmdList, RHICmdList.GetBoundComputeShader(), LodCSParameters, PackConstBufferData, PackConstBufferSize);//#TODO: 去掉远近平面? 

		//Barrier Batch
		FRHITransitionInfo GpuCullingPassBarriers[] = {
			FRHITransitionInfo(RenderComponentData.LandscapeClusterLODData_GPU.UAV, ERHIAccess::SRVCompute, ERHIAccess::UAVCompute), //WAR
			FRHITransitionInfo(RenderComponentData.ClusterLodCountUAV_GPU.UAV, ERHIAccess::SRVCompute, ERHIAccess::UAVCompute), //WAR
		};
		RHICmdList.Transition(MakeArrayView(GpuCullingPassBarriers, UE_ARRAY_COUNT(GpuCullingPassBarriers)));

		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ComponentsOriginAndRadiusSRV, RenderComponentData.ComponentOriginAndRadius_GPU.SRV);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClusterLodBufferUAV, RenderComponentData.LandscapeClusterLODData_GPU.UAV);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClusterLodCountUAV_0, RenderComponentData.ClusterLodCountUAV_GPU.UAV);
	}

	void UnBindParameters(FRHICommandList& RHICmdList) {
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClusterLodBufferUAV, nullptr);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClusterLodCountUAV_0, nullptr); //#todo: Always Bind ?
	}

private:
	LAYOUT_FIELD(FShaderParameter, LodCSParameters);
	LAYOUT_FIELD(FShaderResourceParameter, ComponentsOriginAndRadiusSRV);
	LAYOUT_FIELD(FShaderResourceParameter, ClusterLodBufferUAV);
	LAYOUT_FIELD(FShaderResourceParameter, ClusterLodCountUAV_0);
};

class FLandscapeGpuCullingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeGpuCullingCS);

public:
	FLandscapeGpuCullingCS() : FGlobalShader() {}

	FLandscapeGpuCullingCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		LandscapeParameters.Bind(Initializer.ParameterMap, TEXT("LandscapeParameters"));
		ViewFrustumPermutedPlanes.Bind(Initializer.ParameterMap, TEXT("ViewFrustumPermutedPlanes"));
		LastFrameViewProjectMatrix.Bind(Initializer.ParameterMap, TEXT("LastFrameViewProjectMatrix"));

		ClusterInputDataSRV.Bind(Initializer.ParameterMap, TEXT("ClusterInputDataSRV"));
		HzbResourceBufferSRV.Bind(Initializer.ParameterMap, TEXT("HzbResourceBufferSRV"));
		ClusterLodBufferSRV.Bind(Initializer.ParameterMap, TEXT("ClusterLodBufferSRV"));

		ClusterOutBufferUAV.Bind(Initializer.ParameterMap, TEXT("ClusterOutBufferUAV"));
		ClusterLodCountUAV.Bind(Initializer.ParameterMap, TEXT("ClusterLodCountUAV"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) {
		return true;
	}

	void BindParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FLandscapeGpuRenderProxyComponent_RenderThread& RenderComponentData) {

		//TArray<FVector4, TInlineAllocator<4>> 
		//See detailed definition in shader
		FUintVector4 PackConstBuffer = FUintVector4(
			RenderComponentData.LandscapeComponentSize.X, 
			RenderComponentData.LandscapeComponentSize.Y, 
			RenderComponentData.ClusterSizePerSection * RenderComponentData.NumSections,
			0
		);

		SetShaderValue(RHICmdList, RHICmdList.GetBoundComputeShader(), LandscapeParameters, PackConstBuffer);
		SetShaderValueArray(RHICmdList, RHICmdList.GetBoundComputeShader(), ViewFrustumPermutedPlanes, View.ViewFrustum.PermutedPlanes.GetData(), View.ViewFrustum.PermutedPlanes.Num());
		SetShaderValue(RHICmdList, RHICmdList.GetBoundComputeShader(), LastFrameViewProjectMatrix, View.PrevViewInfo.ViewMatrices.GetViewProjectionMatrix());

		//Barrier Batch
		FRHITransitionInfo GpuCullingPassBarriers[] = {
			FRHITransitionInfo(RenderComponentData.LandscapeClusterLODData_GPU.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute), //RAW
			FRHITransitionInfo(RenderComponentData.ClusterOutputData_GPU.UAV, ERHIAccess::SRVCompute, ERHIAccess::UAVCompute), //WAR
			FRHITransitionInfo(RenderComponentData.ClusterLodCountUAV_GPU.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute), //WAW
			//#todo: batch?
			FRHITransitionInfo(FMobileHzbSystem::GetStructuredBufferRes()->UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute), //RAW
		};
		RHICmdList.Transition(MakeArrayView(GpuCullingPassBarriers, UE_ARRAY_COUNT(GpuCullingPassBarriers)));

		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClusterInputDataSRV, RenderComponentData.ClusterInputData_GPU.SRV);
		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClusterLodBufferSRV, RenderComponentData.LandscapeClusterLODData_GPU.SRV);
		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), HzbResourceBufferSRV, FMobileHzbSystem::GetStructuredBufferRes()->SRV);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClusterOutBufferUAV, RenderComponentData.ClusterOutputData_GPU.UAV);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClusterLodCountUAV, RenderComponentData.ClusterLodCountUAV_GPU.UAV);
	}

	void UnBindParameters(FRHICommandList& RHICmdList) {
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClusterOutBufferUAV, nullptr);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClusterLodCountUAV, nullptr); //#todo: Always Bind ?
	}

private:
	LAYOUT_FIELD(FShaderParameter, LandscapeParameters);
	LAYOUT_FIELD(FShaderParameter, ViewFrustumPermutedPlanes);
	LAYOUT_FIELD(FShaderParameter, LastFrameViewProjectMatrix);
	LAYOUT_FIELD(FShaderResourceParameter, ClusterInputDataSRV);
	LAYOUT_FIELD(FShaderResourceParameter, HzbResourceBufferSRV);
	LAYOUT_FIELD(FShaderResourceParameter, ClusterLodBufferSRV);
	LAYOUT_FIELD(FShaderResourceParameter, ClusterOutBufferUAV);
	LAYOUT_FIELD(FShaderResourceParameter, ClusterLodCountUAV);
};

class FLandscapeGpuSortedCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeGpuSortedCS);

public:
	FLandscapeGpuSortedCS() : FGlobalShader() {}

	FLandscapeGpuSortedCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ClusterOutBufferSRV.Bind(Initializer.ParameterMap, TEXT("ClusterOutBufferSRV"));
		ClusterLodCountSRV.Bind(Initializer.ParameterMap, TEXT("ClusterLodCountSRV"));
		OrderClusterOutBufferUAV.Bind(Initializer.ParameterMap, TEXT("OrderClusterOutBufferUAV"));
		DrawCommandBufferUAV.Bind(Initializer.ParameterMap, TEXT("DrawCommandBufferUAV"));
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) {
		return true;
	}

	void BindParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FLandscapeGpuRenderProxyComponent_RenderThread& RenderComponentData) {
		//Barrier Batch
		FRHITransitionInfo GpuCullingPassBarriers[] = {
			FRHITransitionInfo(RenderComponentData.ClusterOutputData_GPU.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute), //RAW
			FRHITransitionInfo(RenderComponentData.ClusterLodCountUAV_GPU.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute), //RAW
			FRHITransitionInfo(RenderComponentData.OrderClusterOutBufferUAV_GPU.UAV, ERHIAccess::SRVGraphics, ERHIAccess::UAVCompute), //WAR
			FRHITransitionInfo(RenderComponentData.IndirectDrawCommandBuffer_GPU.UAV, ERHIAccess::IndirectArgs, ERHIAccess::UAVCompute) //WAR
		};
		RHICmdList.Transition(MakeArrayView(GpuCullingPassBarriers, UE_ARRAY_COUNT(GpuCullingPassBarriers)));

		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClusterOutBufferSRV, RenderComponentData.ClusterOutputData_GPU.SRV);
		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClusterLodCountSRV, RenderComponentData.ClusterLodCountUAV_GPU.SRV);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), OrderClusterOutBufferUAV, RenderComponentData.OrderClusterOutBufferUAV_GPU.UAV);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), DrawCommandBufferUAV, RenderComponentData.IndirectDrawCommandBuffer_GPU.UAV);
	}

	void UnBindParameters(FRHICommandList& RHICmdList) {
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), OrderClusterOutBufferUAV, nullptr);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), DrawCommandBufferUAV, nullptr); //#todo: Always Bind ?
	}

private:

	LAYOUT_FIELD(FShaderResourceParameter, ClusterOutBufferSRV);
	LAYOUT_FIELD(FShaderResourceParameter, ClusterLodCountSRV);
	LAYOUT_FIELD(FShaderResourceParameter, OrderClusterOutBufferUAV);
	LAYOUT_FIELD(FShaderResourceParameter, DrawCommandBufferUAV);
};

IMPLEMENT_SHADER_TYPE(, FComputeLandscapeLodCS, TEXT("/Engine/Private/LandscapeGpuRender.usf"), TEXT("ClusterComputeLODCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FLandscapeGpuCullingCS, TEXT("/Engine/Private/LandscapeGpuRender.usf"), TEXT("LandscapeGpuCullingCS"), SF_Compute)
IMPLEMENT_SHADER_TYPE(, FLandscapeGpuSortedCS, TEXT("/Engine/Private/LandscapeGpuRender.usf"), TEXT("LandscapeGpuSortedCS"), SF_Compute)

void FMobileSceneRenderer::MobileGpuRenderLanscape(FRHICommandListImmediate& RHICmdList) {
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (CVarMobileComputeShaderControl.GetValueOnRenderThread() == 0) {
		return;
	}
#endif
	FMobileLandscapeGPURenderSystem_RenderThread* LandscapeSystem = FMobileLandscapeGPURenderSystem_RenderThread::GetLandscapeGPURenderSystem_RenderThread(Scene->GetWorld()->GetUniqueID());
	if (LandscapeSystem) {
		for (auto& ComponentPair : LandscapeSystem->LandscapeGpuRenderComponent_RenderThread) {
			FLandscapeGpuRenderProxyComponent_RenderThread& RenderComponent = ComponentPair.Value;
			RenderComponent.UpdateAllGPUBuffer();
			//Calculate All ClusterLod
			{
				const uint32 ThreadGroups = FMath::DivideAndRoundUp(RenderComponent.NumRegisterComponent, ThreadCount);
				TShaderMapRef<FComputeLandscapeLodCS> ComputeLandscapeLodCS(GetGlobalShaderMap(FeatureLevel));
				RHICmdList.SetComputeShader(ComputeLandscapeLodCS.GetComputeShader());
				ComputeLandscapeLodCS->BindParameters(RHICmdList, Views[0], RenderComponent);
				RHICmdList.DispatchComputeShader(ThreadGroups, 1, 1);
				ComputeLandscapeLodCS->UnBindParameters(RHICmdList);
			}

			//Culling, PackData, CalculateLodCount
			{
				const uint32 ThreadGroupsX = FMath::DivideAndRoundUp(RenderComponent.ClusterSizeX, ThreadCount_1);
				const uint32 ThreadGroupsY = FMath::DivideAndRoundUp(RenderComponent.ClusterSizeY, ThreadCount_1);
				TShaderMapRef<FLandscapeGpuCullingCS> LandscapeGpuCullingCS(GetGlobalShaderMap(FeatureLevel));
				RHICmdList.SetComputeShader(LandscapeGpuCullingCS.GetComputeShader());
				LandscapeGpuCullingCS->BindParameters(RHICmdList, Views[0], RenderComponent);
				RHICmdList.DispatchComputeShader(ThreadGroupsX, ThreadGroupsY, 1);
				LandscapeGpuCullingCS->UnBindParameters(RHICmdList);
			}

			//Write DrawCommand and arrange ClusterOutBufferUAV
			{
				const uint32 ThreadGroups = FMath::DivideAndRoundUp(RenderComponent.ClusterSizeX * RenderComponent.ClusterSizeY, ThreadCount);
				TShaderMapRef<FLandscapeGpuSortedCS> LandscapeGpuSortedCS(GetGlobalShaderMap(FeatureLevel));
				RHICmdList.SetComputeShader(LandscapeGpuSortedCS.GetComputeShader());
				LandscapeGpuSortedCS->BindParameters(RHICmdList, Views[0], RenderComponent);
				RHICmdList.DispatchComputeShader(ThreadGroups, 1, 1);
				LandscapeGpuSortedCS->UnBindParameters(RHICmdList);
			}

			//Submit to Graphics
			{
				FRHITransitionInfo UpdateIndirectBufferPassBarriers[] = {
					FRHITransitionInfo(RenderComponent.IndirectDrawCommandBuffer_GPU.UAV, ERHIAccess::UAVCompute, ERHIAccess::IndirectArgs), //RAW
					FRHITransitionInfo(RenderComponent.OrderClusterOutBufferUAV_GPU.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVGraphics), //RAW
					//FRHITransitionInfo(RenderComponent.ClusterLodCountUAV_GPU.UAV, ERHIAccess::SRVCompute, ERHIAccess::SRVGraphics) //RAR... just need StageMask
				};
				RHICmdList.Transition(MakeArrayView(UpdateIndirectBufferPassBarriers, UE_ARRAY_COUNT(UpdateIndirectBufferPassBarriers)));
			}
		}
	}
}

bool bUseLandscapeGpuDriven(const FViewInfo& View) {
	return CVarMobileLandscapeGpuRender.GetValueOnRenderThread() != 0 && !View.bIsSceneCapture;
}
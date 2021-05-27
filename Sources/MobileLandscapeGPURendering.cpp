#include "MobileLandscapeGPURendering.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "MobileHZB.h"
#include "LandscapeMobileGPURenderEngine.h"

constexpr uint32 ThreadCount = 64;

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
			FRHITransitionInfo(RenderComponentData.LandscapeClusterLODData_GPU.UAV, ERHIAccess::SRVCompute, ERHIAccess::UAVCompute) //WAR
		};
		RHICmdList.Transition(MakeArrayView(GpuCullingPassBarriers, UE_ARRAY_COUNT(GpuCullingPassBarriers)));

		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ComponentsOriginAndRadiusSRV, RenderComponentData.ComponentOriginAndRadius_GPU.SRV);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClusterLodBufferUAV, RenderComponentData.LandscapeClusterLODData_GPU.UAV);
	}

	void UnBindParameters(FRHICommandList& RHICmdList) {
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ClusterLodBufferUAV, nullptr);
	}

private:
	LAYOUT_FIELD(FShaderParameter, LodCSParameters);
	LAYOUT_FIELD(FShaderResourceParameter, ComponentsOriginAndRadiusSRV);
	LAYOUT_FIELD(FShaderResourceParameter, ClusterLodBufferUAV);
};
IMPLEMENT_SHADER_TYPE(, FComputeLandscapeLodCS, TEXT("/Engine/Private/LandscapeGpuRender.usf"), TEXT("ClusterComputeLODCS"), SF_Compute)

void FMobileSceneRenderer::MobileGpuRenderLanscape(FRHICommandListImmediate& RHICmdList) {
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
		}
	}
}

bool bUseLandscapeGpuDriven(const FViewInfo& View) {
	return CVarMobileLandscapeGpuRender.GetValueOnRenderThread() != 0 && !View.bIsSceneCapture;
}
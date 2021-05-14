#include "LandscapeMobileGPURender.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "MeshMaterialShader.h"


//TMap<FMobileLandscapeGPURenderSystem::TSystemKey, FMobileLandscapeGPURenderSystem*> FMobileLandscapeGPURenderSystem::LandscapeGPURenderSystem_GameThread;
TMap<uint32, FMobileLandscapeGPURenderSystem*> FMobileLandscapeGPURenderSystem::LandscapeGPURenderSystem_GameThread;
TMap<uint32, FMobileLandscapeGPURenderSystem*> FMobileLandscapeGPURenderSystem::LandscapeGPURenderSystem_RenderThread;

FMobileLandscapeGPURenderSystem::FMobileLandscapeGPURenderSystem(/*uint32 NumComponents*/)
	//: NumComponents(0)
	: NumAllRegisterComponents_GameThread(0)
	, NumAllRegisterComponents_RenderThread(0)
{

}

FMobileLandscapeGPURenderSystem::~FMobileLandscapeGPURenderSystem() {

}

void FMobileLandscapeGPURenderSystem::RegisterGPURenderLandscapeEntity(ULandscapeComponent* LandscapeComponent) {
	check(IsInGameThread());
	check(LandscapeComponent->GetWorld());
	const bool bMobileFeatureLevel = GEngine->GetDefaultWorldFeatureLevel() == ERHIFeatureLevel::ES3_1 || LandscapeComponent->GetWorld()->FeatureLevel == ERHIFeatureLevel::ES3_1;
	if (bMobileFeatureLevel) {
		check(LandscapeComponent->GetLandscapeProxy());
		check((LandscapeComponent->SubsectionSizeQuads + 1) * LandscapeComponent->NumSubsections >= LandscapeGpuRenderParameter::ClusterQuadSize);
		uint32 UniqueWorldIndex = LandscapeComponent->GetWorld()->GetUniqueID();
		FLandscapeSubmitData SubmitToRenderThreadComponentData = FLandscapeSubmitData::CreateLandscapeSubmitData(LandscapeComponent);
		uint32 NumComponents = LandscapeComponent->GetLandscapeProxy()->LandscapeComponents.Num();

		//Create System if null
		FMobileLandscapeGPURenderSystem** FoundSystemPtr = LandscapeGPURenderSystem_GameThread.Find(UniqueWorldIndex);
		FMobileLandscapeGPURenderSystem* FoundSystem = FoundSystemPtr == nullptr ? LandscapeGPURenderSystem_GameThread.Emplace(UniqueWorldIndex, new FMobileLandscapeGPURenderSystem()) : *FoundSystemPtr;
		FoundSystem->NumAllRegisterComponents_GameThread += 1;

		//Create LandscapeProxyComponent if needed
		ULandscapeGpuRenderProxyComponent** ComponentPtr = FoundSystem->LandscapeGpuRenderPeoxyComponens_GameThread.Find(SubmitToRenderThreadComponentData.LandscapeKey);
		if (ComponentPtr == nullptr) {
			ULandscapeGpuRenderProxyComponent* NewComponent = NewObject<ULandscapeGpuRenderProxyComponent>(LandscapeComponent->GetLandscapeProxy(), NAME_None);
			NewComponent->Init(LandscapeComponent);
			FoundSystem->LandscapeGpuRenderPeoxyComponens_GameThread.Emplace(SubmitToRenderThreadComponentData.LandscapeKey, NewComponent);
		}
		else {
			ULandscapeGpuRenderProxyComponent* ComponentRef = *ComponentPtr;
			const auto& ComponentQuadBase = LandscapeComponent->GetSectionBase();
			FVector ComponentMaxBox = FVector(LandscapeComponent->CachedLocalBox.Max.X + ComponentQuadBase.X, LandscapeComponent->CachedLocalBox.Max.Y + ComponentQuadBase.Y, LandscapeComponent->CachedLocalBox.Max.Z);
			ComponentRef->ProxyLocalBox += FBox(LandscapeComponent->CachedLocalBox.Min, ComponentMaxBox); //Calculate the boundingbox
			ComponentRef->NumComponents += 1;
			//ComponentRef->CheckMaterial(LandscapeComponent); //Debug Only?

			//数据准备完毕创建渲染结构
			if (ComponentRef->NumComponents == NumComponents) {
				//maybe by InvalidateLightingCache called
				if (!ComponentRef->IsRegistered()) {
					ComponentRef->RegisterComponent();
				}
			}
		}

		//submit to renderthread
		ENQUEUE_RENDER_COMMAND(RegisterGPURenderLandscapeEntity)(
			[FoundSystem, SubmitToRenderThreadComponentData](FRHICommandList& RHICmdList) {
				RegisterGPURenderLandscapeEntity_RenderThread(FoundSystem, SubmitToRenderThreadComponentData);
			}
		);
	}

}

void FMobileLandscapeGPURenderSystem::UnRegisterGPURenderLandscapeEntity(ULandscapeComponent* LandscapeComponent) {
	check(IsInGameThread());
	check(LandscapeComponent->GetWorld());
	check(LandscapeComponent->GetLandscapeProxy());
	uint32 UniqueWorldIndex = LandscapeComponent->GetWorld()->GetUniqueID();
	FMobileLandscapeGPURenderSystem** FoundSystemPtr = LandscapeGPURenderSystem_GameThread.Find(UniqueWorldIndex);
	if (FoundSystemPtr) {

		//System Release
		FMobileLandscapeGPURenderSystem* FoundSystem = *FoundSystemPtr;
		FoundSystem->NumAllRegisterComponents_GameThread -= 1;
		if (FoundSystem->NumAllRegisterComponents_GameThread == 0) {
			LandscapeGPURenderSystem_GameThread.Remove(UniqueWorldIndex);
		}

		//Component Release
		const FGuid& LandscapeGuid = LandscapeComponent->GetLandscapeProxy()->GetLandscapeGuid();
		ULandscapeGpuRenderProxyComponent* ComponentRef = FoundSystem->LandscapeGpuRenderPeoxyComponens_GameThread.FindChecked(LandscapeGuid);
		ComponentRef->NumComponents -= 1;
		if (ComponentRef->NumComponents == 0) {
			ComponentRef->DestroyComponent(); //Or automatically release by GC
			FoundSystem->LandscapeGpuRenderPeoxyComponens_GameThread.Remove(LandscapeGuid);
		}

		//submit to renderthread
		FLandscapeSubmitData SubmitToRenderThreadComponentData = FLandscapeSubmitData::CreateLandscapeSubmitData(LandscapeComponent);
		ENQUEUE_RENDER_COMMAND(UnRegisterGPURenderLandscapeEntity)(
			[SubmitToRenderThreadComponentData](FRHICommandList& RHICmdList) {
				UnRegisterGPURenderLandscapeEntity_RenderThread(SubmitToRenderThreadComponentData);
			}
		);
	}
}

void FMobileLandscapeGPURenderSystem::RegisterGPURenderLandscapeEntity_RenderThread(FMobileLandscapeGPURenderSystem* RenderSystem, const FLandscapeSubmitData& SubmitToRenderThreadComponentData){
	check(IsInRenderingThread());
	LandscapeGPURenderSystem_RenderThread.Emplace(SubmitToRenderThreadComponentData.UniqueWorldId, RenderSystem);
	FLandscapeGpuRenderData& LandscapeIns = RenderSystem->LandscapeGpuRenderDataMap_RenderThread.FindOrAdd(SubmitToRenderThreadComponentData.LandscapeKey, FLandscapeGpuRenderData());
	LandscapeIns.RegisterComponent(SubmitToRenderThreadComponentData);
	RenderSystem->NumAllRegisterComponents_RenderThread += 1;
}

void FMobileLandscapeGPURenderSystem::UnRegisterGPURenderLandscapeEntity_RenderThread(const FLandscapeSubmitData& SubmitToRenderThreadComponentData) {
	check(IsInRenderingThread());
	FMobileLandscapeGPURenderSystem* RenderSystem = LandscapeGPURenderSystem_RenderThread.FindChecked(SubmitToRenderThreadComponentData.UniqueWorldId);
	FLandscapeGpuRenderData& LandscapeIns = RenderSystem->LandscapeGpuRenderDataMap_RenderThread.FindChecked(SubmitToRenderThreadComponentData.LandscapeKey);
	//Release Component
	LandscapeIns.UnRegisterComponent();
	if (LandscapeIns.NumRegisterComponent == 0) {
		RenderSystem->LandscapeGpuRenderDataMap_RenderThread.Remove(SubmitToRenderThreadComponentData.LandscapeKey); 
	}

	//Release System
	RenderSystem->NumAllRegisterComponents_RenderThread -= 1;
	if (RenderSystem->NumAllRegisterComponents_RenderThread == 0) {
		LandscapeGPURenderSystem_RenderThread.Remove(SubmitToRenderThreadComponentData.UniqueWorldId); //End of life of the system container
		delete RenderSystem;
	}
}

FLandscapeSubmitData FLandscapeSubmitData::CreateLandscapeSubmitData(ULandscapeComponent* LandscapeComponent) {
	FLandscapeSubmitData RetSubmitData;
	RetSubmitData.UniqueWorldId = LandscapeComponent->GetWorld()->GetUniqueID();
	RetSubmitData.ClusterSizePerComponent = (LandscapeComponent->SubsectionSizeQuads + 1) * LandscapeComponent->NumSubsections / LandscapeGpuRenderParameter::ClusterQuadSize;
	RetSubmitData.ComponentBase = LandscapeComponent->GetSectionBase() / LandscapeComponent->ComponentSizeQuads;
	RetSubmitData.LandscapeKey = LandscapeComponent->GetLandscapeProxy()->GetLandscapeGuid();
	return RetSubmitData;
}

//------------------------------------------------VertexFactory------------------------------------------------//
bool FLandscapeGpuRenderVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return GetMaxSupportedFeatureLevel(Parameters.Platform) == ERHIFeatureLevel::ES3_1 &&
		(Parameters.MaterialParameters.bIsUsedWithLandscape || Parameters.MaterialParameters.bIsSpecialEngineMaterial);
}

void FLandscapeGpuRenderVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("NUM_VF_PACKED_INTERPOLANTS"), TEXT("1"));
}

FLandscapeClusterVertexBuffer::FLandscapeClusterVertexBuffer() {
	INC_DWORD_STAT_BY(STAT_LandscapeVertexMem, LandscapeGpuRenderParameter::ClusterVertexDataSize);
	InitResource();
}

FLandscapeClusterVertexBuffer::~FLandscapeClusterVertexBuffer(){
	ReleaseResource();
	//VertexMemory Calculate
	DEC_DWORD_STAT_BY(STAT_LandscapeVertexMem, LandscapeGpuRenderParameter::ClusterVertexDataSize);
}

void FLandscapeClusterVertexBuffer::InitRHI() {
	SCOPED_LOADTIMER(FLandscapeClusterVertexBuffer_InitRHI);

	// create a static vertex buffer
	uint32 VertexSize = LandscapeGpuRenderParameter::ClusterQuadSize + 1;
	FRHIResourceCreateInfo CreateInfo;
	void* BufferData = nullptr;
	VertexBufferRHI = RHICreateAndLockVertexBuffer(VertexSize * VertexSize * sizeof(FLandscapeClusterVertex), BUF_Static, CreateInfo, BufferData);
	FLandscapeClusterVertex* Vertex = reinterpret_cast<FLandscapeClusterVertex*>(BufferData);

	for (uint32 y = 0; y < VertexSize; y++) {
		for (uint32 x = 0; x < VertexSize; x++) {
			Vertex->PositionX = static_cast<uint8>(x);
			Vertex->PositionY = static_cast<uint8>(y);
			Vertex->Blank_0 = 0;
			Vertex->Blank_1 = 0;
			Vertex++;
		}
	}
	RHIUnlockVertexBuffer(VertexBufferRHI);
}

//------------------------------------------------SceneProxy------------------------------------------------//
SIZE_T FLandscapeGpuRenderProxyComponentSceneProxy::GetTypeHash() const{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FLandscapeGpuRenderProxyComponentSceneProxy::ApplyWorldOffset(FVector InOffset) {
	
}

bool FLandscapeGpuRenderProxyComponentSceneProxy::CanBeOccluded() const {
	return false; //hardware Occlusion Culling
}

void FLandscapeGpuRenderProxyComponentSceneProxy::GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const {
	FPrimitiveSceneProxy::GetLightRelevance(LightSceneProxy, bDynamic, bRelevant, bLightMapped, bShadowMapped);
}

void FLandscapeGpuRenderProxyComponentSceneProxy::OnTransformChanged() {

}

void FLandscapeGpuRenderProxyComponentSceneProxy::CreateRenderThreadResources() {

}

void FLandscapeGpuRenderProxyComponentSceneProxy::DestroyRenderThreadResources() {

}

void FLandscapeGpuRenderProxyComponentSceneProxy::OnLevelAddedToWorld() {

}

FLandscapeGpuRenderProxyComponentSceneProxy::FLandscapeGpuRenderProxyComponentSceneProxy(ULandscapeGpuRenderProxyComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	check(GetScene().GetFeatureLevel() == ERHIFeatureLevel::ES3_1);
	AvailableMaterials.Append(InComponent->MobileMaterialInterfaces);
}

FPrimitiveViewRelevance FLandscapeGpuRenderProxyComponentSceneProxy::GetViewRelevance(const FSceneView* View) const {
	FPrimitiveViewRelevance Result;
	//const bool bCollisionView = (View->Family->EngineShowFlags.CollisionVisibility || View->Family->EngineShowFlags.CollisionPawn);
	//Result.bDrawRelevance = (IsShown(View) || bCollisionView) && View->Family->EngineShowFlags.Landscape;
	Result.bDrawRelevance = IsShown(View) && View->Family->EngineShowFlags.Landscape;
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
	Result.bDynamicRelevance = true;
	Result.bStaticRelevance = false;
	Result.bShadowRelevance = IsShadowCast(View) && View->Family->EngineShowFlags.Landscape;
	return Result;
}

void FLandscapeGpuRenderProxyComponentSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const {
	if (AvailableMaterials.Num() == 0) {
		return;
	}
	check(AvailableMaterials.Num() != 0);
	UMaterialInterface* MaterialInterface = AvailableMaterials[0];
	check(MaterialInterface != nullptr);

	for (int LodIndex = 0; LodIndex < LandscapeGpuRenderParameter::ClusterLod; ++LodIndex) {
		FMeshBatch MeshBatch;
		MeshBatch.VertexFactory = VertexFactory;
		MeshBatch.MaterialRenderProxy = MaterialInterface->GetRenderProxy();
		MeshBatch.LCI = nullptr; //don't need to any bake info
		MeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();
		MeshBatch.CastShadow = true; //值来自于FPrimitiveFlagsCompact, 所以这里无所谓
		MeshBatch.bUseForDepthPass = true;
		MeshBatch.bUseAsOccluder = false;
		MeshBatch.bUseForMaterial = true;
		MeshBatch.Type = PT_TriangleList;
		MeshBatch.DepthPriorityGroup = SDPG_World;
		MeshBatch.LODIndex = LodIndex; //not need
		MeshBatch.bDitheredLODTransition = false;
		MeshBatch.bCanApplyViewModeOverrides = true; //兼容WireFrame等
		//MeshBatch.bUseWireframeSelectionColoring = IsSelected(); //选中颜色

		// Combined batch element
		FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
		BatchElement.UserData = &ComponentBatchUserData;
		BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
		BatchElement.IndexBuffer = SharedBuffers->ClusterIndexBuffers[ClusterLod];
		BatchElement.NumPrimitives = 0; //Use indirect
		BatchElement.FirstIndex = 0; //Use IndirectArgs don't need
		BatchElement.MinVertexIndex = 0; //Use IndirectArgs don't need
		BatchElement.MaxVertexIndex = 0; //Use IndirectArgs don't need
		BatchElement.NumInstances = 0;  //Use IndirectArgs don't need
		BatchElement.InstancedLODIndex = 0; //用来传递LOD, don't need

		Collector.AddMesh(0, MeshBatch);
	}
}
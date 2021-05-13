#include "LandscapeMobileGPURender.h"
#include "ProfilingDebugging/LoadTimeTracker.h"


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

FLandscapeGpuRenderProxyComponentSceneProxy::FLandscapeGpuRenderProxyComponentSceneProxy(ULandscapeGpuRenderProxyComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	
}

void FLandscapeGpuRenderProxyComponentSceneProxy::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI){
	if (AvailableMaterials.Num() == 0){
		return;
	}
	check(AvailableMaterials.Num() != 0);
	UMaterialInterface* MaterialInterface = AvailableMaterials[0];
	check(MaterialInterface != nullptr);

	// Based on the final material we selected, detect if it has tessellation
	FMeshBatch MeshBatch;


	MeshBatch.VertexFactory = VertexFactory;
	MeshBatch.MaterialRenderProxy = MaterialInterface->GetRenderProxy();

	MeshBatch.LCI = nullptr; //don't need to any bake info
	MeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();
	MeshBatch.CastShadow = true; //
	MeshBatch.bUseForDepthPass = true;
	MeshBatch.bUseAsOccluder = ShouldUseAsOccluder() && GetScene().GetShadingPath() == EShadingPath::Deferred && !IsMovable();
	MeshBatch.bUseForMaterial = true;
	MeshBatch.Type = PT_TriangleList;
	MeshBatch.DepthPriorityGroup = SDPG_World;
	MeshBatch.LODIndex = LODIndex;
	MeshBatch.bDitheredLODTransition = false;

	// Combined batch element
	FMeshBatchElement& BatchElement = MeshBatch.Elements[0];

	FLandscapeBatchElementParams* BatchElementParams = new(OutStaticBatchParamArray) FLandscapeBatchElementParams;
	BatchElementParams->LandscapeUniformShaderParametersResource = &LandscapeUniformShaderParameters;
	BatchElementParams->SceneProxy = this;
	BatchElementParams->CurrentLOD = LODIndex;

	BatchElement.UserData = BatchElementParams;
	BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
	BatchElement.IndexBuffer = bCurrentRequiresAdjacencyInformation ? SharedBuffers->AdjacencyIndexBuffers->IndexBuffers[LODIndex] : SharedBuffers->IndexBuffers[LODIndex];
	BatchElement.NumPrimitives = FMath::Square((SubsectionSizeVerts >> LODIndex) - 1) * FMath::Square(NumSubsections) * 2;
	BatchElement.FirstIndex = 0;
	BatchElement.MinVertexIndex = SharedBuffers->IndexRanges[LODIndex].MinIndexFull;
	BatchElement.MaxVertexIndex = SharedBuffers->IndexRanges[LODIndex].MaxIndexFull;

	// The default is overridden here only by mobile landscape to punch holes in the geometry
	//ApplyMeshElementModifier(BatchElement, LODIndex);

	int32 TotalBatchCount = 1 + LastLOD - FirstLOD;
	TotalBatchCount += (1 + LastVirtualTextureLOD - FirstVirtualTextureLOD) * RuntimeVirtualTextureMaterialTypes.Num();

	StaticBatchParamArray.Empty(TotalBatchCount);
	PDI->ReserveMemoryForMeshes(TotalBatchCount);

	//We no longer need the original Virtual Texture, use new algrithms
	for (int32 LODIndex = FirstLOD; LODIndex <= LastLOD; LODIndex++){
		FMeshBatch MeshBatch;

		if (GetStaticMeshElement(LODIndex, false, false, MeshBatch, StaticBatchParamArray)){
			PDI->DrawMesh(MeshBatch, LODIndex == FirstLOD ? FLT_MAX : (FMath::Sqrt(LODScreenRatioSquared[LODIndex]) * 2.0f));
		}
	}

	check(StaticBatchParamArray.Num() <= TotalBatchCount);
}

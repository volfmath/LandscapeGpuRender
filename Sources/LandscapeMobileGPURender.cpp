#include "LandscapeMobileGPURender.h"
#include "ProfilingDebugging/LoadTimeTracker.h"


//TMap<FMobileLandscapeGPURenderSystem::TSystemKey, FMobileLandscapeGPURenderSystem*> FMobileLandscapeGPURenderSystem::LandscapeGPURenderSystem_GameThread;
TMap<uint32, FMobileLandscapeGPURenderSystem*> FMobileLandscapeGPURenderSystem::LandscapeGPURenderSystem_GameThread;
TMap<uint32, FMobileLandscapeGPURenderSystem*> FMobileLandscapeGPURenderSystem::LandscapeGPURenderSystem_RenderThread;

FMobileLandscapeGPURenderSystem::FMobileLandscapeGPURenderSystem() 
	: NumRegisterLandscape_GameThread(0)
	, NumRegisterLandscape_RenderThread(0)
{

}

FMobileLandscapeGPURenderSystem::~FMobileLandscapeGPURenderSystem() {

}

void FMobileLandscapeGPURenderSystem::RegisterGPURenderLandscapeEntity(ULandscapeComponent* LandscapeComponent) {
	check(IsInGameThread());
	check(LandscapeComponent->GetWorld());
	check(LandscapeComponent->GetLandscapeProxy());
	check((LandscapeComponent->SubsectionSizeQuads + 1) * LandscapeComponent->NumSubsections >= FLandscapeClusterVertexBuffer::ClusterQuadSize);
	uint32 UniqueWorldIndex = LandscapeComponent->GetWorld()->GetUniqueID();

	FMobileLandscapeGPURenderSystem** FoundSystemPtr = LandscapeGPURenderSystem_GameThread.Find(UniqueWorldIndex);
	FMobileLandscapeGPURenderSystem* FoundSystem = FoundSystemPtr == nullptr ? LandscapeGPURenderSystem_GameThread.Emplace(UniqueWorldIndex,new FMobileLandscapeGPURenderSystem()) : *FoundSystemPtr;
	FoundSystem->NumRegisterLandscape_GameThread += 1;

	//submit to renderthread
	FLandscapeSubmitData SubmitToRenderThreadComponentData = FLandscapeSubmitData::CreateLandscapeSubmitData(LandscapeComponent);
	ENQUEUE_RENDER_COMMAND(RegisterGPURenderLandscapeEntity)(
		[FoundSystem, SubmitToRenderThreadComponentData](FRHICommandList& RHICmdList) {
			RegisterGPURenderLandscapeEntity_RenderThread(FoundSystem, SubmitToRenderThreadComponentData);
		}
	);
}

void FMobileLandscapeGPURenderSystem::UnRegisterGPURenderLandscapeEntity(ULandscapeComponent* LandscapeComponent) {
	check(IsInGameThread());
	check(LandscapeComponent->GetWorld());
	check(LandscapeComponent->GetLandscapeProxy());
	uint32 UniqueWorldIndex = LandscapeComponent->GetWorld()->GetUniqueID();
	FMobileLandscapeGPURenderSystem** FoundSystemPtr = LandscapeGPURenderSystem_GameThread.Find(UniqueWorldIndex);
	if (FoundSystemPtr) {
		FMobileLandscapeGPURenderSystem* FoundSystem = *FoundSystemPtr;
		FoundSystem->NumRegisterLandscape_GameThread -= 1;
		if (FoundSystem->NumRegisterLandscape_GameThread == 0) {
			LandscapeGPURenderSystem_GameThread.Remove(UniqueWorldIndex);
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
	RenderSystem->NumRegisterLandscape_RenderThread += 1;
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
	RenderSystem->NumRegisterLandscape_RenderThread -= 1;
	if (RenderSystem->NumRegisterLandscape_RenderThread == 0) {
		LandscapeGPURenderSystem_RenderThread.Remove(SubmitToRenderThreadComponentData.UniqueWorldId); //End of life of the system container
		delete RenderSystem;
	}
}

FLandscapeSubmitData FLandscapeSubmitData::CreateLandscapeSubmitData(ULandscapeComponent* LandscapeComponent) {
	FLandscapeSubmitData RetSubmitData;
	RetSubmitData.UniqueWorldId = LandscapeComponent->GetWorld()->GetUniqueID();
	RetSubmitData.ClusterSizePerComponent = (LandscapeComponent->SubsectionSizeQuads + 1) * LandscapeComponent->NumSubsections / FLandscapeClusterVertexBuffer::ClusterQuadSize;
	RetSubmitData.ComponentBase = LandscapeComponent->GetSectionBase() / LandscapeComponent->ComponentSizeQuads;
	RetSubmitData.LandscapeKey = LandscapeComponent->GetLandscapeProxy()->GetLandscapeGuid();
	return RetSubmitData;
}

FLandscapeClusterVertexBuffer::FLandscapeClusterVertexBuffer() {
	INC_DWORD_STAT_BY(STAT_LandscapeVertexMem, ClusterVertexDataSize);
	InitResource();
}

FLandscapeClusterVertexBuffer::~FLandscapeClusterVertexBuffer(){
	ReleaseResource();
	//VertexMemory Calculate
	DEC_DWORD_STAT_BY(STAT_LandscapeVertexMem, ClusterVertexDataSize);
}

void FLandscapeClusterVertexBuffer::InitRHI() {
	SCOPED_LOADTIMER(FLandscapeClusterVertexBuffer_InitRHI);

	// create a static vertex buffer
	uint32 VertexSize = ClusterQuadSize + 1;
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



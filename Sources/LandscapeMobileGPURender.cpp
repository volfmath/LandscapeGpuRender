#include "LandscapeMobileGPURender.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "MeshMaterialShader.h"
#include "SceneView.h"
#include "MobileGpuDriven.h"
#include "LandscapeGpuRenderProxyComponent.h"

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FLandscapeGpuRenderUniformBuffer, "LandscapeGpuRenderUniformBuffer");

TMap<uint32, FMobileLandscapeGPURenderSystem_GameThread*> FMobileLandscapeGPURenderSystem_GameThread::LandscapeGPURenderSystem_GameThread;

FMobileLandscapeGPURenderSystem_GameThread::FMobileLandscapeGPURenderSystem_GameThread(/*uint32 NumComponents*/)
	//: NumComponents(0)
	: NumAllRegisterComponents_GameThread(0)
{

}

FMobileLandscapeGPURenderSystem_GameThread::~FMobileLandscapeGPURenderSystem_GameThread() {
	check(NumAllRegisterComponents_GameThread == 0);
}

void FMobileLandscapeGPURenderSystem_GameThread::RegisterGPURenderLandscapeEntity(ULandscapeComponent* LandscapeComponent) {
	const bool bMobileFeatureLevel = GEngine->GetDefaultWorldFeatureLevel() == ERHIFeatureLevel::ES3_1 || LandscapeComponent->GetWorld()->FeatureLevel == ERHIFeatureLevel::ES3_1;
	if (CVarMobileLandscapeGpuRender.GetValueOnGameThread() != 0 && bMobileFeatureLevel) {
		check(IsInGameThread());
		check(LandscapeComponent->GetWorld());
		check(LandscapeComponent->GetLandscapeProxy());
		check((LandscapeComponent->SubsectionSizeQuads + 1) >= LandscapeGpuRenderParameter::ClusterQuadSize);		
		const FLandscapeSubmitData& SubmitToRenderThreadComponentData = FLandscapeSubmitData::CreateLandscapeSubmitData(LandscapeComponent);

		//At first, Submit to renderthread
		//Put the code in front because FLandscapeGpuRenderProxyComponentSceneProxy will use data
		ENQUEUE_RENDER_COMMAND(RegisterGPURenderLandscapeEntity)(
			[SubmitToRenderThreadComponentData](FRHICommandList& RHICmdList) {
				FMobileLandscapeGPURenderSystem_RenderThread::RegisterGPURenderLandscapeEntity_RenderThread(SubmitToRenderThreadComponentData);
			}
		);

		uint32 UniqueWorldIndex = LandscapeComponent->GetWorld()->GetUniqueID();
		uint32 NumComponents = LandscapeComponent->GetLandscapeProxy()->LandscapeComponents.Num();

		//Create System if null
		FMobileLandscapeGPURenderSystem_GameThread** FoundSystemPtr = LandscapeGPURenderSystem_GameThread.Find(UniqueWorldIndex);
		FMobileLandscapeGPURenderSystem_GameThread* FoundSystem = FoundSystemPtr == nullptr ? LandscapeGPURenderSystem_GameThread.Emplace(UniqueWorldIndex, new FMobileLandscapeGPURenderSystem_GameThread()) : *FoundSystemPtr;
		FoundSystem->NumAllRegisterComponents_GameThread += 1;

		//Create LandscapeProxyComponent if needed
		ULandscapeGpuRenderProxyComponent** ComponentPtr = FoundSystem->LandscapeGpuRenderPeoxyComponens_GameThread.Find(SubmitToRenderThreadComponentData.LandscapeKey);
		if (ComponentPtr == nullptr) {
			ULandscapeGpuRenderProxyComponent* NewComponent = NewObject<ULandscapeGpuRenderProxyComponent>(LandscapeComponent->GetLandscapeProxy(), NAME_None);
			NewComponent->Init(LandscapeComponent);
			ULandscapeGpuRenderProxyComponent*& EmplaceComponent = FoundSystem->LandscapeGpuRenderPeoxyComponens_GameThread.Emplace(SubmitToRenderThreadComponentData.LandscapeKey, NewComponent);
			ComponentPtr = &EmplaceComponent;
		}
		else {
			ULandscapeGpuRenderProxyComponent* ComponentRef = *ComponentPtr;
			ComponentRef->UpdateBoundingInformation(LandscapeComponent->CachedLocalBox, LandscapeComponent->GetSectionBase());
			//Check resources
			ComponentRef->CheckResources(LandscapeComponent);
		}

		ULandscapeGpuRenderProxyComponent* ComponentRef = *ComponentPtr;
		if (ComponentRef->NumComponents == NumComponents) {
			//Maybe by InvalidateLightingCache called, so we need to check the status of register
			if (!ComponentRef->IsRegistered()) {
				//RegisterTo FScene, call AddPrimitive
				ComponentRef->RegisterComponent();
			}

			if (!ComponentRef->IsClusterBoundingCreated()) {
				ComponentRef->CreateClusterBoundingBox(SubmitToRenderThreadComponentData);
			}
		}
	}
}

void FMobileLandscapeGPURenderSystem_GameThread::UnRegisterGPURenderLandscapeEntity(ULandscapeComponent* LandscapeComponent) {
	//const bool bMobileFeatureLevel = GEngine->GetDefaultWorldFeatureLevel() == ERHIFeatureLevel::ES3_1 || LandscapeComponent->GetWorld()->FeatureLevel == ERHIFeatureLevel::ES3_1;
	//if (CVarMobileLandscapeGpuRender.GetValueOnGameThread() != 0 && bMobileFeatureLevel) {
	//Uninstallation does not need to judge the flag, because only the registered Component can query the valid information
	check(IsInGameThread());
	check(LandscapeComponent->GetWorld());
	check(LandscapeComponent->GetLandscapeProxy());
	uint32 UniqueWorldIndex = LandscapeComponent->GetWorld()->GetUniqueID();
	FMobileLandscapeGPURenderSystem_GameThread** FoundSystemPtr = LandscapeGPURenderSystem_GameThread.Find(UniqueWorldIndex);
	if (FoundSystemPtr) {
		//System Release
		FMobileLandscapeGPURenderSystem_GameThread* FoundSystem = *FoundSystemPtr;
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

		//Submit to renderthread
		const FLandscapeSubmitData& SubmitToRenderThreadComponentData = FLandscapeSubmitData::CreateLandscapeSubmitData(LandscapeComponent);
		ENQUEUE_RENDER_COMMAND(UnRegisterGPURenderLandscapeEntity)(
			[SubmitToRenderThreadComponentData](FRHICommandList& RHICmdList) {
				FMobileLandscapeGPURenderSystem_RenderThread::UnRegisterGPURenderLandscapeEntity_RenderThread(SubmitToRenderThreadComponentData);
			}
		);
	}
}

FLandscapeSubmitData FLandscapeSubmitData::CreateLandscapeSubmitData(ULandscapeComponent* LandscapeComponent) {
	FLandscapeSubmitData RetSubmitData;
	RetSubmitData.UniqueWorldId = LandscapeComponent->GetWorld()->GetUniqueID();
	RetSubmitData.NumSections = LandscapeComponent->NumSubsections;
	RetSubmitData.ClusterSizePerSection = (LandscapeComponent->SubsectionSizeQuads + 1) / LandscapeGpuRenderParameter::ClusterQuadSize;
	RetSubmitData.ComponentBase = LandscapeComponent->GetSectionBase() / LandscapeComponent->ComponentSizeQuads;
	RetSubmitData.LandscapeKey = LandscapeComponent->GetLandscapeProxy()->GetLandscapeGuid();

	//LodParameters
	{
		//No need for LOD0ScreenSizeSquared, because it is not used to calculate integer LOD
		//LOD0
		const auto NumClusterLod = FMath::CeilLogTwo(LandscapeGpuRenderParameter::ClusterQuadSize) + 1;
		const auto NumSectionLod = FMath::CeilLogTwo(LandscapeComponent->SubsectionSizeQuads + 1);
		check(NumSectionLod > NumClusterLod);

		//面积递减系数为ScreenSizeRatioDivider
		float ScreenSizeRatioDivider = FMath::Max(LandscapeComponent->GetLandscapeProxy()->LOD0DistributionSetting, 1.01f);
		float CurrentScreenSizeRatio = LandscapeComponent->GetLandscapeProxy()->LOD0ScreenSize;
		uint8 LastLODIndex = LandscapeGpuRenderParameter::ClusterLodCount - 1;
		float LOD0ScreenSizeSquared = FMath::Square(CurrentScreenSizeRatio);

		//LOD1
		CurrentScreenSizeRatio /= ScreenSizeRatioDivider;
		float LOD1ScreenSizeSquared = FMath::Square(CurrentScreenSizeRatio);

		//Other Lod
		ScreenSizeRatioDivider = FMath::Max(LandscapeComponent->GetLandscapeProxy()->LODDistributionSetting, 1.01f);
		float LODOnePlusDistributionScalarSquared = FMath::Square(ScreenSizeRatioDivider);
		for (uint32 i = 1; i < NumClusterLod; ++i) {
			CurrentScreenSizeRatio /= ScreenSizeRatioDivider;
		}
		float LastLODScreenSizeSquared = FMath::Square(CurrentScreenSizeRatio);
		
		//(LastLODScreenSizeSquared, LOD1ScreenSizeSquared, LODOnePlusDistributionScalarSquared, LastLODIndex)
		RetSubmitData.LodSettingParameters = FVector4(LastLODScreenSizeSquared, LOD1ScreenSizeSquared, LODOnePlusDistributionScalarSquared, LastLODIndex);
	}

	//TestBound
	auto TestBound = LandscapeComponent->Bounds;

	return RetSubmitData;
}

//------------------------------------------------VertexFactory------------------------------------------------//
class FLandscapeGpuRenderVertexFactoryVSParameters : public FVertexFactoryShaderParameters{
	DECLARE_INLINE_TYPE_LAYOUT(FLandscapeGpuRenderVertexFactoryVSParameters, NonVirtual);
public:
	/**
	* Bind shader constants by name
	* @param	ParameterMap - mapping of named shader constants to indices
	*/
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		LandscapeGpuRenderOutput.Bind(ParameterMap, TEXT("LandscapeGpuRenderOutputBuffer"));
		FirstIndexBuffer.Bind(ParameterMap, TEXT("FirstIndexBuffer"));
		LodIndexParameters.Bind(ParameterMap, TEXT("LodIndexParameters")); //Test
	}

	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const FSceneView* InView,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
	) const
	{
		SCOPE_CYCLE_COUNTER(STAT_LandscapeVFDrawTimeVS);
		const FLandscapeGpuRenderUserData* GpuParameters = reinterpret_cast<const FLandscapeGpuRenderUserData*>(BatchElement.UserData);

		//#Todo: Batch?
		ShaderBindings.Add(LandscapeGpuRenderOutput, GpuParameters->LandscapeGpuRenderOutputBufferSRV);
		ShaderBindings.Add(FirstIndexBuffer, GpuParameters->LandscapeGpuRenderFirstIndexSRV);
		ShaderBindings.Add(LodIndexParameters, BatchElement.UserIndex);

		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeGpuRenderUniformBuffer>(), GpuParameters->LandscapeGpuRenderUniformBuffer);
	
	}

protected:
	LAYOUT_FIELD(FShaderResourceParameter, LandscapeGpuRenderOutput)
	LAYOUT_FIELD(FShaderResourceParameter, FirstIndexBuffer)
	LAYOUT_FIELD(FShaderParameter, LodIndexParameters) //Test
};

class FLandscapeGpuRenderVertexFactoryPSParameters : public FVertexFactoryShaderParameters{
	DECLARE_INLINE_TYPE_LAYOUT(FLandscapeGpuRenderVertexFactoryPSParameters, NonVirtual);
public:
	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const FSceneView* InView,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
	) const
	{
		SCOPE_CYCLE_COUNTER(STAT_LandscapeVFDrawTimePS);
		//const FLandscapeClusterBatchElementParams* BatchElementParams = (const FLandscapeClusterBatchElementParams*)BatchElement.UserData;
		//ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeComponentClusterUniformBuffer>(), *BatchElementParams->LandscapeComponentClusterUniformBuffer);
	}
};

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeGpuRenderVertexFactory, SF_Vertex, FLandscapeGpuRenderVertexFactoryVSParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeGpuRenderVertexFactory, SF_Pixel, FLandscapeGpuRenderVertexFactoryPSParameters);
IMPLEMENT_VERTEX_FACTORY_TYPE(FLandscapeGpuRenderVertexFactory, "/Engine/Private/LandscapeGpuRenderVertexFactory.ush", true, true, true, false, false);

bool FLandscapeGpuRenderVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters){
	return GetMaxSupportedFeatureLevel(Parameters.Platform) == ERHIFeatureLevel::ES3_1 &&
		(Parameters.MaterialParameters.bIsUsedWithLandscape || Parameters.MaterialParameters.bIsSpecialEngineMaterial);
}

void FLandscapeGpuRenderVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment){
	FVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("NUM_VF_PACKED_INTERPOLANTS"), TEXT("1"));
}

void FLandscapeGpuRenderVertexFactory::Copy(const FLandscapeGpuRenderVertexFactory& Other) {
	FLandscapeGpuRenderVertexFactory* VertexFactory = this;
	const FDataType* DataCopy = &Other.MobileData;
	ENQUEUE_RENDER_COMMAND(FLandscapeVertexFactoryCopyData)(
		[VertexFactory, DataCopy](FRHICommandListImmediate& RHICmdList)
		{
			VertexFactory->MobileData = *DataCopy;
		});
	BeginUpdateResourceRHI(this);
}

void FLandscapeGpuRenderVertexFactory::InitRHI() {
	// list of declaration items
	FVertexDeclarationElementList Elements;

	// position decls
	Elements.Add(AccessStreamComponent(MobileData.PositionComponent, 0));

	// create the actual device decls
	InitDeclaration(Elements);
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
			Vertex->PositionX = static_cast<float>(x);
			Vertex->PositionY = static_cast<float>(y);

			//Vertex->PositionX = static_cast<uint8>(x);
			//Vertex->PositionY = static_cast<uint8>(y);
			//Vertex->Blank_0 = 0;
			//Vertex->Blank_1 = 0;
			Vertex += 1;
		}
	}
	RHIUnlockVertexBuffer(VertexBufferRHI);
}

//------------------------------------------------SceneProxy------------------------------------------------//
template<typename IndexType>
void FLandscapeGpuRenderProxyComponentSceneProxy::CreateClusterIndexBuffers(TArray<FIndexBuffer*>& InIndexBuffers) {
	constexpr uint32 ClusterVertSize = LandscapeGpuRenderParameter::ClusterQuadSize + 1;
	for (int32 LodLevel = 0; LodLevel < InIndexBuffers.Num(); LodLevel++) {
		TArray<IndexType> NewIndices;
		uint16 LodClusterQuadSize = LandscapeGpuRenderParameter::ClusterQuadSize >> LodLevel;
		uint32 ExpectedNumIndices = LodClusterQuadSize * LodClusterQuadSize * 6;
		NewIndices.Empty(ExpectedNumIndices);

		for (uint32 y = 0; y < LodClusterQuadSize; ++y) {
			for (uint32 x = 0; x < LodClusterQuadSize; ++x) {
				IndexType i00 = y * ClusterVertSize + x;
				IndexType i10 = y * ClusterVertSize + x + 1;
				IndexType i11 = (y + 1) * ClusterVertSize + x + 1;
				IndexType i01 = (y + 1) * ClusterVertSize + x;

				NewIndices.Add(i00);
				NewIndices.Add(i11);
				NewIndices.Add(i10);

				NewIndices.Add(i00);
				NewIndices.Add(i01);
				NewIndices.Add(i11);
			}
		}

		FRawStaticIndexBuffer16or32<IndexType>* RawIndexBuffer = new FRawStaticIndexBuffer16or32<IndexType>(false);
		RawIndexBuffer->AssignNewBuffer(NewIndices);
		RawIndexBuffer->InitResource();
		InIndexBuffers[LodLevel] = static_cast<FIndexBuffer*>(RawIndexBuffer);
	}
}

const static FName NAME_GpuRenderLandscapeResourceNameForDebugging(TEXT("GpuRenderLandscape"));
FLandscapeGpuRenderProxyComponentSceneProxy::FLandscapeGpuRenderProxyComponentSceneProxy(ULandscapeGpuRenderProxyComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent, NAME_GpuRenderLandscapeResourceNameForDebugging)
	, UniqueWorldId(InComponent->GetWorld()->GetUniqueID())
	, NumClusterPerSection((InComponent->SectionSizeQuads + 1) / LandscapeGpuRenderParameter::ClusterQuadSize)
	, SectionSizeQuads(InComponent->SectionSizeQuads)
	, VertexFactory(nullptr)
	, VertexBuffer(nullptr)
	, LandscapeKey(InComponent->LandscapeKey)
	, HeightmapTexture(InComponent->HeightmapTexture)
{
	check(GetScene().GetFeatureLevel() == ERHIFeatureLevel::ES3_1);
	for (int32 i = 0; i < InComponent->MobileMaterialInterfaces.Num(); ++i) {
		AvailableMaterials.Emplace(InComponent->MobileMaterialInterfaces[i].Get());
	}
}

FLandscapeGpuRenderProxyComponentSceneProxy::~FLandscapeGpuRenderProxyComponentSceneProxy() {
	check(VertexFactory == nullptr);
	check(VertexBuffer == nullptr);
}

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
	// cache component's WorldToLocal
	FMatrix LocalToWorldNoScaling = GetLocalToWorld();
	LocalToWorldNoScaling.RemoveScaling();

	FLandscapeGpuRenderUniformBuffer LandscapeGpuRenderParams;
	LandscapeGpuRenderParams.NumClusterPerSection = NumClusterPerSection;
	LandscapeGpuRenderParams.QuadSizeParameter = FVector2D(LandscapeGpuRenderParameter::ClusterQuadSize, SectionSizeQuads);

	//Calculate the HeightmapUVParameter
	FVector4 HeightmapUVParameter = FVector4(
		((float)(SectionSizeQuads + 1) / (float)FMath::Max<int32>(1, HeightmapTexture->GetSizeX())),
		((float)(SectionSizeQuads + 1) / (float)FMath::Max<int32>(1, HeightmapTexture->GetSizeY())),
		1.0f / (float)HeightmapTexture->GetSizeX(), 
		1.0f / (float)HeightmapTexture->GetSizeY()
	);
	LandscapeGpuRenderParams.HeightmapUVParameter = HeightmapUVParameter;
	LandscapeGpuRenderParams.LocalToWorldNoScaling = LocalToWorldNoScaling;

	//int32 WeightmapSize = ComponentSizeVerts;
	//WeightmapScaleBias = FVector4(1.0f / (float)WeightmapSize, 1.0f / (float)WeightmapSize, 0.5f / (float)WeightmapSize, 0.5f / (float)WeightmapSize);
	//WeightmapSubsectionOffset = (float)(SubsectionSizeQuads + 1) / (float)WeightmapSize;

	//SetHeightmap
	LandscapeGpuRenderParams.HeightmapTexture = HeightmapTexture->TextureReference.TextureReferenceRHI;
	LandscapeGpuRenderParams.HeightmapTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();

	if (!LandscapeGpuRenderUniformBuffer.IsValid()) {
		LandscapeGpuRenderUniformBuffer = TUniformBufferRef<FLandscapeGpuRenderUniformBuffer>::CreateUniformBufferImmediate(LandscapeGpuRenderParams, UniformBuffer_MultiFrame);
	}
	else {
		LandscapeGpuRenderUniformBuffer.UpdateUniformBufferImmediate(LandscapeGpuRenderParams);
	}	

	//Upload the UniformBuffer to GpuRenderProxyComponent
	FLandscapeGpuRenderProxyComponent_RenderThread& GpuRenderDataRef = FMobileLandscapeGPURenderSystem_RenderThread::GetLandscapeGPURenderComponent_RenderThread(UniqueWorldId, LandscapeKey);
	GpuRenderDataRef.LandscapeGpuRenderUserData.LandscapeGpuRenderUniformBuffer = LandscapeGpuRenderUniformBuffer.GetReference();
}

void FLandscapeGpuRenderProxyComponentSceneProxy::CreateRenderThreadResources() {
	check(VertexBuffer == nullptr);
	check(IndexBuffers.Num() == 0);
	check(VertexFactory == nullptr);

	//Create and init VertexBuffer
	VertexBuffer = new FLandscapeClusterVertexBuffer(); //Construct call InitResource

	//Create and init IndexBuffer	
	static_assert(LandscapeGpuRenderParameter::ClusterQuadSize * LandscapeGpuRenderParameter::ClusterQuadSize * 6 < 0x10000, ""); //Just support int16
	IndexBuffers.AddZeroed(LandscapeGpuRenderParameter::ClusterLodCount);
	FLandscapeGpuRenderProxyComponentSceneProxy::CreateClusterIndexBuffers<uint16>(IndexBuffers);

	//Create and init VertexFactory
	auto FeatureLevel = GetScene().GetFeatureLevel();
	VertexFactory = new FLandscapeGpuRenderVertexFactory(FeatureLevel);
	VertexFactory->MobileData.PositionComponent = FVertexStreamComponent(VertexBuffer, 0, sizeof(FLandscapeClusterVertex), /*VET_UByte4N*/VET_Float2);
	VertexFactory->InitResource();
}

void FLandscapeGpuRenderProxyComponentSceneProxy::DestroyRenderThreadResources() {
	ensure(VertexFactory != nullptr);
	ensure(VertexBuffer != nullptr);
	ensure(IndexBuffers.Num() == LandscapeGpuRenderParameter::ClusterLodCount);

	delete VertexFactory;
	VertexFactory = nullptr;

	delete VertexBuffer;
	VertexBuffer = nullptr;

	for (int32 i = 0; i < IndexBuffers.Num(); i++) {
		IndexBuffers[i]->ReleaseResource();
		delete IndexBuffers[i];
	}
}

void FLandscapeGpuRenderProxyComponentSceneProxy::OnLevelAddedToWorld() {

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
	if (Views[0]->bIsSceneCapture) {
		return;
	}

	UMaterialInterface* MaterialInterface = AvailableMaterials[0];
	const auto& GpuRenderData = FMobileLandscapeGPURenderSystem_RenderThread::GetLandscapeGPURenderComponent_RenderThread(UniqueWorldId, LandscapeKey);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (ViewFamily.EngineShowFlags.Bounds) {
		FColor StartingColor = FColor(100, 0, 0);
		for (const FBoxSphereBounds& DrawBounds : GpuRenderData.WorldClusterBounds) {		
			DrawWireBox(Collector.GetPDI(0), DrawBounds.GetBox(), StartingColor, SDPG_World);
			StartingColor.R += 5;
			StartingColor.G += 5;
			StartingColor.B += 5;
		}		
	}
#endif
	for (int LodIndex = 0; LodIndex < LandscapeGpuRenderParameter::ClusterLodCount; ++LodIndex) {
		FMeshBatch& MeshBatch = Collector.AllocateMesh();
		MeshBatch.VertexFactory = VertexFactory;
		MeshBatch.MaterialRenderProxy = MaterialInterface->GetRenderProxy();
		MeshBatch.LCI = nullptr; //don't need to any bake info
		MeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();
		MeshBatch.CastShadow = true; //The value comes from FPrimitiveFlagsCompact, so it doesn’t matter here
		MeshBatch.bUseForDepthPass = true;
		MeshBatch.bUseAsOccluder = false;
		MeshBatch.bUseForMaterial = true;
		MeshBatch.Type = PT_TriangleList;
		MeshBatch.DepthPriorityGroup = SDPG_World;
		MeshBatch.LODIndex = LodIndex; //don't need
		MeshBatch.bDitheredLODTransition = false;
		MeshBatch.bCanApplyViewModeOverrides = true; //兼容WireFrame等
		//MeshBatch.bUseWireframeSelectionColoring = IsSelected(); //选中颜色

		// Combined batch element
		FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
		BatchElement.UserData = &GpuRenderData.LandscapeGpuRenderUserData;
		BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
		BatchElement.IndexBuffer = IndexBuffers[LodIndex];
		BatchElement.NumPrimitives = 0; //Use indirect
		BatchElement.FirstIndex = 0; //Use IndirectArgs don't need
		BatchElement.MinVertexIndex = 0; //Use IndirectArgs don't need
		BatchElement.MaxVertexIndex = 0; //Use IndirectArgs don't need
		BatchElement.NumInstances = 0;  //Use IndirectArgs don't need
		BatchElement.InstancedLODIndex = 0; //用来传递LOD, don't need
		BatchElement.IndirectArgsBuffer = GpuRenderData.IndirectDrawCommandBuffer_GPU.Buffer;
		BatchElement.IndirectArgsOffset = LodIndex * sizeof(FDrawIndirectCommandArgs_CPU);

		BatchElement.UserIndex = LodIndex; // Test

		Collector.AddMesh(0, MeshBatch);
	}
}
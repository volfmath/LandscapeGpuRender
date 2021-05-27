#include "LandscapeGpuRenderProxyComponent.h"
#include "LandscapeComponent.h"
#include "LandscapeProxy.h"
#include "LandscapeMobileGPURender.h"
#include "LandscapeDataAccess.h"

ULandscapeGpuRenderProxyComponent::ULandscapeGpuRenderProxyComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bIsClusterBoundingCreated(false)
	, NumComponents(0)
	, ComponentSectionSize(0)
	, SectionSizeQuads(0)
	, HeightmapTexture(nullptr)
	, ProxyLocalBox(ForceInit)
//#if WITH_EDITORONLY_DATA
//	, CachedEditingLayerData(nullptr)
//	, LayerUpdateFlagPerMode(0)
//	, WeightmapsHash(0)
//	, SplineHash(0)
//	, PhysicalMaterialHash(0)
//#endif
//	, GrassData(MakeShareable(new FLandscapeComponentGrassData()))
//	, ChangeTag(0)
{
	//SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);

	bUseAsOccluder = true;
	bAllowCullDistanceVolume = false;
	//CollisionMipLevel = 0;
	//StaticLightingResolution = 0.f; // Default value 0 means no overriding

	//MaterialInstances.AddDefaulted(); // make sure we always have a MaterialInstances[0]	
	//LODIndexToMaterialIndex.AddDefaulted(); // make sure we always have a MaterialInstances[0]	

	//HeightmapScaleBias = FVector4(0.0f, 0.0f, 0.0f, 1.0f);
	//WeightmapScaleBias = FVector4(0.0f, 0.0f, 0.0f, 1.0f);

	bBoundsChangeTriggersStreamingDataRebuild = false;
	//ForcedLOD = -1;
	//LODBias = 0;
//#if WITH_EDITORONLY_DATA
//	LightingLODBias = -1; // -1 Means automatic LOD calculation based on ForcedLOD + LODBias
//#endif

	Mobility = EComponentMobility::Static;

//#if WITH_EDITORONLY_DATA
//	EditToolRenderData = FLandscapeEditToolRenderData();
//#endif

	//LpvBiasMultiplier = 0.0f; // Bias is 0 for landscape, since it's single sided

	// We don't want to load this on the server, this component is for graphical purposes only
	AlwaysLoadOnServer = false;

	// Default sort priority of landscape to -1 so that it will default to the first thing rendered in any runtime virtual texture
	TranslucencySortPriority = -1;

	//LODStreamingProxy = CreateDefaultSubobject<ULandscapeLODStreamingProxy>(TEXT("LandscapeLODStreamingProxy"));
}

ULandscapeGpuRenderProxyComponent::~ULandscapeGpuRenderProxyComponent() {
	//check NumComponents
	check(NumComponents == 0);
}

FBoxSphereBounds ULandscapeGpuRenderProxyComponent::CalcBounds(const FTransform& LocalToWorld) const{
	return FBoxSphereBounds(ProxyLocalBox.TransformBy(LocalToWorld));
}

FPrimitiveSceneProxy* ULandscapeGpuRenderProxyComponent::CreateSceneProxy() {
	//May be called by the construct of default Uobject, So we need to check status
	if (NumComponents != 0) {
		return new FLandscapeGpuRenderProxyComponentSceneProxy(this);
	}
	else {
		return nullptr;
	}
}

void ULandscapeGpuRenderProxyComponent::Init(ULandscapeComponent* LandscapeComponent) {
	NumComponents = 1; //Initial always 1

	//Set SectionSizeQuads
	SectionSizeQuads = LandscapeComponent->SubsectionSizeQuads;
	ComponentSectionSize = LandscapeComponent->NumSubsections;

	//Set LandscapeKey
	LandscapeKey = LandscapeComponent->GetLandscapeProxy()->GetLandscapeGuid();

	//Set BoundingBox
	const auto& ComponentQuadBase = LandscapeComponent->GetSectionBase();
	FVector ComponentMaxBox = FVector(LandscapeComponent->CachedLocalBox.Max.X + ComponentQuadBase.X, LandscapeComponent->CachedLocalBox.Max.Y + ComponentQuadBase.Y, LandscapeComponent->CachedLocalBox.Max.Z);
	ProxyLocalBox = FBox(LandscapeComponent->CachedLocalBox.Min, ComponentMaxBox);
	check(LandscapeComponent->CachedLocalBox.Min.X == 0 && LandscapeComponent->CachedLocalBox.Min.Y == 0);
	
	//Save MaterialInsterface
	check(LandscapeComponent->MobileMaterialInterfaces.Num() > 0);
	MobileMaterialInterfaces.Reserve(LandscapeComponent->MobileMaterialInterfaces.Num());
	for (int32 Index = 0; Index < LandscapeComponent->MobileMaterialInterfaces.Num(); ++Index) {
		MobileMaterialInterfaces.Emplace(TWeakObjectPtr<UMaterialInterface>(LandscapeComponent->MobileMaterialInterfaces[Index]));
		check(MobileMaterialInterfaces[Index].IsValid());
	}

	//Save HeightMap
	HeightmapTexture = LandscapeComponent->HeightmapTexture;

	//Set transform
	SetRelativeLocation(FVector::ZeroVector);
	SetupAttachment(LandscapeComponent->GetLandscapeProxy()->GetRootComponent(), NAME_None);
}


ALandscapeProxy* ULandscapeGpuRenderProxyComponent::GetLandscapeProxy() const{
	return CastChecked<ALandscapeProxy>(GetOuter());
}

void ULandscapeGpuRenderProxyComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const{
	// TODO - investigate whether this is correct

	//ALandscapeProxy* Actor = GetLandscapeProxy();

	//if (Actor != nullptr && Actor->bUseDynamicMaterialInstance)
	//{
	//	OutMaterials.Append(MaterialInstancesDynamic.FilterByPredicate([](UMaterialInstanceDynamic* MaterialInstance) { return MaterialInstance != nullptr; }));
	//}
	//else
	//{
	//	OutMaterials.Append(MaterialInstances.FilterByPredicate([](UMaterialInstanceConstant* MaterialInstance) { return MaterialInstance != nullptr; }));
	//}

	//if (OverrideMaterial)
	//{
	//	OutMaterials.Add(OverrideMaterial);
	//}

	//if (OverrideHoleMaterial)
	//{
	//	OutMaterials.Add(OverrideHoleMaterial);
	//}
	OutMaterials.Reserve(MobileMaterialInterfaces.Num());
	for (int32 Index = 0; Index < MobileMaterialInterfaces.Num(); ++Index) {
		OutMaterials.Emplace(MobileMaterialInterfaces[Index].Get());
	}

//#if WITH_EDITORONLY_DATA
//	if (EditToolRenderData.ToolMaterial)
//	{
//		OutMaterials.Add(EditToolRenderData.ToolMaterial);
//	}
//
//	if (EditToolRenderData.GizmoMaterial)
//	{
//		OutMaterials.Add(EditToolRenderData.GizmoMaterial);
//	}
//#endif

//#if WITH_EDITOR
//	//if (bGetDebugMaterials) // TODO: This should be tested and enabled
//	{
//		OutMaterials.Add(GLayerDebugColorMaterial);
//		OutMaterials.Add(GSelectionColorMaterial);
//		OutMaterials.Add(GSelectionRegionMaterial);
//		OutMaterials.Add(GMaskRegionMaterial);
//		OutMaterials.Add(GColorMaskRegionMaterial);
//		OutMaterials.Add(GLandscapeLayerUsageMaterial);
//		OutMaterials.Add(GLandscapeDirtyMaterial);
//	}
//#endif
}

void ULandscapeGpuRenderProxyComponent::UpdateBoundingInformation(const FBox& ComponentCachedLocalBox, const FIntPoint& ComponentQuadBase) {
	//Component are local coordinate conversion is required
	FVector ComponentMaxBox = FVector(ComponentCachedLocalBox.Max.X + ComponentQuadBase.X, ComponentCachedLocalBox.Max.Y + ComponentQuadBase.Y, ComponentCachedLocalBox.Max.Z);
	ProxyLocalBox += FBox(ComponentCachedLocalBox.Min, ComponentMaxBox); //Calculate the boundingbox
	NumComponents += 1;
}

void ULandscapeGpuRenderProxyComponent::CheckResources(ULandscapeComponent* LandscapeComponent) {
	check(HeightmapTexture != nullptr);
	check(HeightmapTexture == LandscapeComponent->HeightmapTexture);
}

void ULandscapeGpuRenderProxyComponent::CreateClusterBounding(const FLandscapeSubmitData& LandscapeSubmitData) {
	FVector BoundingSize = ProxyLocalBox.GetSize();

	//The total section size of the landscape
	const uint32 SectionSizeX = static_cast<uint32>(BoundingSize.X) / SectionSizeQuads;
	const uint32 SectionSizeY = static_cast<uint32>(BoundingSize.Y) / SectionSizeQuads;
	const uint32 LandscapeComponentSizeX = SectionSizeX / ComponentSectionSize;
	const uint32 LandscapeComponentSizeY = SectionSizeY / ComponentSectionSize;
	const uint32 SectionVerts = SectionSizeQuads + 1;
	
	//Cluster parameters
	const uint32 ClusterSizePerSection = (SectionSizeQuads + 1) / LandscapeGpuRenderParameter::ClusterQuadSize;
	const uint32 ClusterSizeX = ClusterSizePerSection * SectionSizeX;
	const uint32 ClusterSizeY = ClusterSizePerSection * SectionSizeY;
	const uint32 ClusterSizePerComponent = ClusterSizePerSection * ComponentSectionSize;
	const uint32 ClusterSqureSizePerComponent = ClusterSizePerComponent * ClusterSizePerComponent;
	check(SectionSizeX * SectionSizeY == NumComponents * ComponentSectionSize * ComponentSectionSize);

	//Check Heightmap
	uint32 HeightMapSizeX = SectionVerts * SectionSizeX;
	uint32 HeightMapSizeY = SectionVerts * SectionSizeY;

	//CPU读取贴图必须满足如下3个条件才能从PlatformData读取数据
	//	CompressionSettings 是否为VectorDisplacementmap。
	//	MipGenSettings 是否为NoMipmaps。
	//	SRGB 是否为未勾选状态。
	//#TODO: 干掉Mip
	//#TODO: 序列化数据,因为高度数据没有存放在cpu中,Runtime下无法获取
#if WITH_EDITOR
	FColor* HeightMapData = reinterpret_cast<FColor*>(HeightmapTexture->Source.LockMip(0));
	//check(HeightMapSizeX >= static_cast<uint32>(HeightmapTexture->GetSizeX()) && HeightMapSizeY >= static_cast<uint32>(HeightmapTexture->GetSizeY()));
	//FTexture2DMipMap& MipData = HeightmapTexture->PlatformData->Mips[0];
	//FColor* HeightMapData = reinterpret_cast<FColor*>(MipData.BulkData.Lock(LOCK_READ_ONLY));
#endif

	//Calculate the BoundingBox
	//The vertices are exactly aligned to the power of 2, so there is no need to calculate whether they are on the edge or clamp
	//The memory layout is unified for each Component linear arrangement
	TArray<FBox> SubmitToRenderThreadBoundingBox;
	SubmitToRenderThreadBoundingBox.Reserve(ClusterSizeX * ClusterSizeY);

	for (uint32 CompoenntY = 0; CompoenntY < LandscapeComponentSizeY; ++CompoenntY) {
		for (uint32 ComponentX = 0; ComponentX < LandscapeComponentSizeX; ++ComponentX) {
			for (uint32 LocalClusterIndexY = 0; LocalClusterIndexY < ClusterSizePerComponent; ++LocalClusterIndexY) {
				for (uint32 LocalClusterIndexX = 0; LocalClusterIndexX < ClusterSizePerComponent; ++LocalClusterIndexX) {
					FIntPoint GlobalClusterIndex = FIntPoint(LocalClusterIndexX + ComponentX * ClusterSizePerComponent, LocalClusterIndexY + CompoenntY * ClusterSizePerComponent);
					//FLandscapeGpuRenderProxyComponent_RenderThread::GetLinearIndexByClusterIndex(GlobalClusterIndex);
					//Create Box
					FVector VertexStartPos = FVector(
						(GlobalClusterIndex.X & (ClusterSizePerSection - 1)) * LandscapeGpuRenderParameter::ClusterQuadSize + GlobalClusterIndex.X / ClusterSizePerSection * SectionSizeQuads,
						(GlobalClusterIndex.Y & (ClusterSizePerSection - 1)) * LandscapeGpuRenderParameter::ClusterQuadSize + GlobalClusterIndex.Y / ClusterSizePerSection * SectionSizeQuads,
						0.f
					);

					FVector VertexEndPos = FVector(
						((GlobalClusterIndex.X + 1) & (ClusterSizePerSection - 1)) * LandscapeGpuRenderParameter::ClusterQuadSize + (GlobalClusterIndex.X + 1) / ClusterSizePerSection * SectionSizeQuads,
						((GlobalClusterIndex.Y + 1) & (ClusterSizePerSection - 1)) * LandscapeGpuRenderParameter::ClusterQuadSize + (GlobalClusterIndex.Y + 1) / ClusterSizePerSection * SectionSizeQuads,
						0.f
					);

					FBox& BoxRef = SubmitToRenderThreadBoundingBox.Emplace_GetRef(VertexStartPos, VertexEndPos);
					//Calculte Vertex
					for (uint32 VertexY = 0; VertexY < LandscapeGpuRenderParameter::ClusterQuadSize; ++VertexY) {
						for (uint32 VertexX = 0; VertexX < LandscapeGpuRenderParameter::ClusterQuadSize; ++VertexX) {
							//SampleIndex use VertSize instead of SectionQuadsize
							uint32 SampleX = VertexX + (GlobalClusterIndex.X & (ClusterSizePerSection - 1)) * LandscapeGpuRenderParameter::ClusterQuadSize
								+ GlobalClusterIndex.X / ClusterSizePerSection * SectionVerts;
							uint32 SampleY = (VertexY + GlobalClusterIndex.Y * LandscapeGpuRenderParameter::ClusterQuadSize) * HeightMapSizeX;
							uint32 HeightMapSampleIndex = SampleX + SampleY;
							const auto& HeightValue = HeightMapData[HeightMapSampleIndex];
							float VertexHeight = LandscapeDataAccess::GetLocalHeight(static_cast<uint16>(HeightValue.R << 8u | HeightValue.G));

							//Update the Box
							BoxRef.Min.Z = FMath::Min(BoxRef.Min.Z, VertexHeight);
							BoxRef.Max.Z = FMath::Max(BoxRef.Max.Z, VertexHeight);
						}
					}
				}
			}
		}
	}
	//for (uint32 ClusterIndexY = 0; ClusterIndexY < ClusterSizeY; ++ClusterIndexY) {
	//	for (uint32 ClusterIndexX = 0; ClusterIndexX < ClusterSizeX; ++ClusterIndexX) {
	//		//Create Box
	//		FVector VertexStartPos = FVector(
	//			(ClusterIndexX & (ClusterSizePerSection - 1)) * LandscapeGpuRenderParameter::ClusterQuadSize + ClusterIndexX / ClusterSizePerSection * SectionSizeQuads,
	//			(ClusterIndexY & (ClusterSizePerSection - 1)) * LandscapeGpuRenderParameter::ClusterQuadSize + ClusterIndexY / ClusterSizePerSection * SectionSizeQuads,
	//			0.f
	//		);

	//		FVector VertexEndPos = FVector(
	//			((ClusterIndexX + 1) & (ClusterSizePerSection - 1)) * LandscapeGpuRenderParameter::ClusterQuadSize + (ClusterIndexX + 1) / ClusterSizePerSection * SectionSizeQuads,
	//			((ClusterIndexY + 1) & (ClusterSizePerSection - 1)) * LandscapeGpuRenderParameter::ClusterQuadSize + (ClusterIndexY + 1) / ClusterSizePerSection * SectionSizeQuads,
	//			0.f
	//		);

	//		FBox& BoxRef = SubmitToRenderThreadBoundingBox.Emplace_GetRef(VertexStartPos, VertexEndPos);

	//	}
	//}

#if WITH_EDITOR
	HeightmapTexture->Source.UnlockMip(0);
#endif

	FMatrix LocalToWorldMatrix = GetRenderMatrix();
	ENQUEUE_RENDER_COMMAND(RegisterGPURenderLandscapeEntity)(
		[ClusterBoundingArray{ MoveTemp(SubmitToRenderThreadBoundingBox)}, LandscapeSubmitData, LocalToWorldMatrix](FRHICommandList& RHICmdList) {
			auto& RenderComponent = FMobileLandscapeGPURenderSystem_RenderThread::GetLandscapeGPURenderComponent_RenderThread(LandscapeSubmitData.UniqueWorldId, LandscapeSubmitData.LandscapeKey);
			RenderComponent.InitClusterData(ClusterBoundingArray, LocalToWorldMatrix);
		}
	);

	bIsClusterBoundingCreated = true;
}

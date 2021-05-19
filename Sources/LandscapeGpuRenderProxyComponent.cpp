#include "LandscapeGpuRenderProxyComponent.h"
#include "LandscapeComponent.h"
#include "LandscapeProxy.h"
#include "LandscapeMobileGPURender.h"

ULandscapeGpuRenderProxyComponent::ULandscapeGpuRenderProxyComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, NumComponents(0)
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
	return new FLandscapeGpuRenderProxyComponentSceneProxy(this);
}

void ULandscapeGpuRenderProxyComponent::Init(ULandscapeComponent* LandscapeComponent) {
	NumComponents = 1; //Initial always 1

	//Set SectionSizeQuads
	SectionSizeQuads = LandscapeComponent->SubsectionSizeQuads;

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
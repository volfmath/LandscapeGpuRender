#pragma once
#include "CoreMinimal.h"
#include "Materials/MaterialInterface.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "LandscapeMobileGPURender.h"

#include "LandscapeGpuRenderProxyComponent.generated.h"

class ULandscapeComponent;
class ALandscapeProxy;

UCLASS()
class ULandscapeGpuRenderProxyComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()

	~ULandscapeGpuRenderProxyComponent();

	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	ALandscapeProxy* GetLandscapeProxy() const;
	void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const;
	void Init(ULandscapeComponent* LandscapeComponent);
	void UpdateBoundingInformation(const FBox& ComponentCachedLocalBox, const FIntPoint& ComponentQuadBase);
	void CheckResources(ULandscapeComponent* LandscapeComponent);
	void CreateClusterBoundingBox(const FLandscapeSubmitData& LandscapeSubmitData);
	inline bool IsClusterBoundingCreated() const { return bIsClusterBoundingCreated; }

public:
	//[Don't Serialize]
	bool bIsClusterBoundingCreated;

	//[Don't Serialize]
	uint32 NumComponents;

	//[Don't Serialize]
	uint32 ComponentSectionSize;

	//[Don't Serialize]
	uint32 SectionSizeQuads;

	//[Don't Serialize]
	UTexture2D* HeightmapTexture; // PC : Heightmap, Mobile : Weightmap

	//[Don't Serialize]
	FBox ProxyLocalBox;

	//[Don't Serialize]
	FGuid LandscapeKey;

	//[Don't Serialize]
	TArray<TWeakObjectPtr<UMaterialInterface>> MobileMaterialInterfaces;
};
#pragma once
#include "CoreMinimal.h"
#include "Materials/MaterialInterface.h"
#include "UObject/WeakObjectPtrTemplates.h"

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
	void CheckResources(ULandscapeComponent* LandscapeComponent);

public:
	//[Don't Serialize]
	uint32 NumComponents;

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
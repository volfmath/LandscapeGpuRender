#pragma once
#include "CoreMinimal.h"
#include "Materials/MaterialInterface.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "LandscapeGpuRenderProxyComponent.generated.h"

class ULandscapeComponent;

UCLASS()
class ULandscapeGpuRenderProxyComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()

	~ULandscapeGpuRenderProxyComponent();

	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	void CheckMaterial(ULandscapeComponent* LandscapeComponent);
	void Init(ULandscapeComponent* LandscapeComponent);

public:
	//[Don't Serialize]
	uint32 NumComponents;

	//[Don't Serialize]
	FBox ProxyLocalBox;

	//[Don't Serialize]
	FGuid LandscapeKey;

	//[Don't Serialize]
	TArray<TWeakObjectPtr<UMaterialInterface>> MobileMaterialInterfaces;
};
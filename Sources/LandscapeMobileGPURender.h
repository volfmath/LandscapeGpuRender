#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "RenderResource.h"
#include "VertexFactory.h"
#include "LandscapeRender.h"
#include "Runtime/Landscape/Private/LandscapePrivate.h"
#include "LandscapeMobileGPURenderEngine.h"
#include "LandscapeGpuRenderProxyComponent.h"

struct FLandscapeClusterVertex;

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLandscapeGpuRenderUniformBuffer, LANDSCAPE_API)
	SHADER_PARAMETER(int32, NumClusterPerSection)
	SHADER_PARAMETER(FVector2D, QuadSizeParameter)
	SHADER_PARAMETER(FVector4, HeightmapUVParameter)
	SHADER_PARAMETER(FMatrix, LocalToWorldNoScaling)
	SHADER_PARAMETER_TEXTURE(Texture2D, HeightmapTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, HeightmapTextureSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

//Submit to landscape data
struct FLandscapeSubmitData {
	static FLandscapeSubmitData CreateLandscapeSubmitData(ULandscapeComponent* LandscapeComponent);
	uint32 UniqueWorldId;
	uint32 NumSections;
	uint32 ClusterSizePerSection;
	FIntPoint ComponentBase;
	FGuid LandscapeKey;
	FVector4 LodSettingParameters;
};

struct FMobileLandscapeGPURenderSystem_GameThread {
	FMobileLandscapeGPURenderSystem_GameThread(/*uint32 NumComponents*/);
	~FMobileLandscapeGPURenderSystem_GameThread();
	static void RegisterGPURenderLandscapeEntity(ULandscapeComponent* InstanceComponent);
	static void UnRegisterGPURenderLandscapeEntity(ULandscapeComponent* InstanceComponent);
	static TMap<uint32, FMobileLandscapeGPURenderSystem_GameThread*> LandscapeGPURenderSystem_GameThread;
	
	//[GameThread]
	uint32 NumAllRegisterComponents_GameThread; //the sum of the Entity numbers of all Landscapes, note that System may have multiple Landscapes
	TMap<FGuid, ULandscapeGpuRenderProxyComponent*> LandscapeGpuRenderPeoxyComponens_GameThread; //Resources Manager
};

class FLandscapeGpuRenderVertexFactory : public FVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FLandscapeGpuRenderVertexFactory);

public:

	struct FDataType{
		/** The stream to read the vertex position from. */
		FVertexStreamComponent PositionComponent;
	};

	FLandscapeGpuRenderVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
		: FVertexFactory(InFeatureLevel)
	{
		
	}

	virtual ~FLandscapeGpuRenderVertexFactory(){
		ReleaseResource();
	}

	/**
	* Should we cache the material's shadertype on this platform with this vertex factory?
	*/
	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);

	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	/**
	* Copy the data from another vertex factory
	* @param Other - factory to copy from
	*/
	void Copy(const FLandscapeGpuRenderVertexFactory& Other);

	// FRenderResource interface.
	virtual void InitRHI() override;

	/**
	 * An implementation of the interface used by TSynchronizedResource to update the resource with new data from the game thread.
	 */
	void SetData(const FDataType& InData){
		MobileData = InData;
		UpdateRHI();
	}

private:
	/** stream component data bound to this vertex factory */
	FDataType MobileData;

	friend class FLandscapeGpuRenderProxyComponentSceneProxy;
};

class FLandscapeClusterVertexBuffer : public FVertexBuffer
{
public:
	FLandscapeClusterVertexBuffer();
	virtual ~FLandscapeClusterVertexBuffer();

	/**
	* Initialize the RHI for this rendering resource
	*/
	virtual void InitRHI() override;
};

class FLandscapeGpuRenderProxyComponentSceneProxy final : public FPrimitiveSceneProxy {
public:
	//[Resources Value]
	uint32 UniqueWorldId;

	//[Resources Value]
	uint32 NumClusterPerSection;

	//[Resources Value]
	uint32 SectionSizeQuads;

	//[Resources Manager]
	FLandscapeGpuRenderVertexFactory* VertexFactory;

	//[Resources Manager]
	FLandscapeClusterVertexBuffer* VertexBuffer;

	//[Resources Manager]
	TArray<FIndexBuffer*> IndexBuffers;

	//[Resources Manager, Auto Release]
	TUniformBufferRef<FLandscapeGpuRenderUniformBuffer> LandscapeGpuRenderUniformBuffer;
	//TUniformBuffer<FLandscapeGpuRenderUniformBuffer> LandscapeGpuRenderUniformBuffer; //TUniformBuffer will store a copy of Content in memory, no need

	//[Resources Value]
	FGuid LandscapeKey;

	//[Resources Ref]
	UTexture2D* HeightmapTexture; // PC : Heightmap, Mobile : Weightmap

	//[Resources Ref]
	TArray<UMaterialInterface*> AvailableMaterials;//Mobile Material, 

	template <typename IndexType>
	static void CreateClusterIndexBuffers(TArray<FIndexBuffer*>& InIndexBuffers);

	SIZE_T GetTypeHash() const override;
	FLandscapeGpuRenderProxyComponentSceneProxy(ULandscapeGpuRenderProxyComponent* InComponent);
	virtual ~FLandscapeGpuRenderProxyComponentSceneProxy();

	// FPrimitiveSceneProxy interface.
	virtual void ApplyWorldOffset(FVector InOffset) override;
	//virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override; assume all is dynamic
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	virtual uint32 GetMemoryFootprint() const override { return(sizeof(*this) + GetAllocatedSize()); }
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual bool CanBeOccluded() const override;
	virtual void GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const override;
	virtual void OnTransformChanged() override;
	virtual void CreateRenderThreadResources() override;
	virtual void DestroyRenderThreadResources() override;
	virtual void OnLevelAddedToWorld() override;
};



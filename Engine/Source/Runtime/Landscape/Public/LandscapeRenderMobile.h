// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
LandscapeRenderMobile.h: Mobile landscape rendering
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "RenderResource.h"
#include "VertexFactory.h"
#include "LandscapeRender.h"
#include "Runtime/Landscape/Private/LandscapePrivate.h"

#define LANDSCAPE_MAX_ES_LOD_COMP	2
#define LANDSCAPE_MAX_ES_LOD		6

struct FLandscapeMobileVertex
{
	uint8 Position[4]; // Pos + LOD 0 Height
	uint8 LODHeights[LANDSCAPE_MAX_ES_LOD_COMP*4];
};

/** vertex factory for VTF-heightmap terrain  */
class FLandscapeVertexFactoryMobile : public FLandscapeVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FLandscapeVertexFactoryMobile);

	typedef FLandscapeVertexFactory Super;
public:

	struct FDataType : FLandscapeVertexFactory::FDataType
	{
		/** stream which has heights of each LOD levels */
		TArray<FVertexStreamComponent,TFixedAllocator<LANDSCAPE_MAX_ES_LOD_COMP> > LODHeightsComponent;
	};

	FLandscapeVertexFactoryMobile(ERHIFeatureLevel::Type InFeatureLevel)
		: FLandscapeVertexFactory(InFeatureLevel)
	{
	}

	virtual ~FLandscapeVertexFactoryMobile()
	{
		ReleaseResource();
	}

	/**
	* Should we cache the material's shadertype on this platform with this vertex factory? 
	*/
	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);

	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
	{
		FVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NUM_VF_PACKED_INTERPOLANTS"), TEXT("1"));
	}

	// FRenderResource interface.
	virtual void InitRHI() override;

	/**
	 * An implementation of the interface used by TSynchronizedResource to update the resource with new data from the game thread.
	 */
	void SetData(const FDataType& InData)
	{
		MobileData = InData;
		UpdateRHI();
	}

private:
	/** stream component data bound to this vertex factory */
	FDataType MobileData; 

	friend class FLandscapeComponentSceneProxyMobile;
};

//
// FLandscapeVertexBuffer
//
class FLandscapeVertexBufferMobile : public FVertexBuffer
{
	TArray<uint8> VertexData;
	int32 DataSize;
public:

	/** Constructor. */
	FLandscapeVertexBufferMobile(TArray<uint8> InVertexData)
	:	VertexData(InVertexData)
	,	DataSize(InVertexData.Num())
	{
		INC_DWORD_STAT_BY(STAT_LandscapeVertexMem, DataSize);
	}

	/** Destructor. */
	virtual ~FLandscapeVertexBufferMobile()
	{
		ReleaseResource();
		DEC_DWORD_STAT_BY(STAT_LandscapeVertexMem, DataSize);
	}

	/** 
	* Initialize the RHI for this rendering resource 
	*/
	virtual void InitRHI() override;
};

//
// FLandscapeComponentSceneProxy
//
class FLandscapeComponentSceneProxyMobile final : public FLandscapeComponentSceneProxy
{
protected:
	TSharedPtr<FLandscapeMobileRenderData, ESPMode::ThreadSafe> MobileRenderData;

	virtual ~FLandscapeComponentSceneProxyMobile();

public:
	SIZE_T GetTypeHash() const override;

	FLandscapeComponentSceneProxyMobile(ULandscapeComponent* InComponent);

	virtual void CreateRenderThreadResources() override;
	virtual int32 CollectOccluderElements(FOccluderElementsCollector& Collector) const override;

	friend class FLandscapeVertexBufferMobile;

	virtual void ApplyMeshElementModifier(FMeshBatchElement& InOutMeshElement, int32 InLodIndex) const override;

	//@StarLight code - BEGIN Optimize terrain LOD, Added by zhuyule
	virtual bool IsUsingCustomLODRules() const;

	virtual struct FLODMask GetCustomLOD(const FSceneView& InView, float InViewLODScale, int32 InForcedLODLevel, float& OutScreenSizeSquared) const;
	//@StarLight code - END Optimize terrain LOD, Added by zhuyule
};


class FLandscapeFixedGridVertexFactoryMobile : public FLandscapeVertexFactoryMobile
{
	DECLARE_VERTEX_FACTORY_TYPE(FLandscapeFixedGridVertexFactoryMobile);

public:
	FLandscapeFixedGridVertexFactoryMobile(ERHIFeatureLevel::Type InFeatureLevel)
		: FLandscapeVertexFactoryMobile(InFeatureLevel)
	{
	}

	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);
};




//@StarLight code - BEGIN LandScapeInstance, Added by yanjianhong---------------------------------------------------------------

class FLandscapeComponentSceneProxyInstanceMobile;

struct FLandscapeClusterBatchElementParams
{
	FLandscapeClusterBatchElementParams(
		const TUniformBuffer<FLandscapeComponentClusterUniformBuffer>* InLandscapeComponentClusterUniformBuffer,
		const FLandscapeComponentSceneProxyInstanceMobile* InSceneProxy,
		const uint32 InCurLOD)
		: LandscapeComponentClusterUniformBuffer(InLandscapeComponentClusterUniformBuffer)
		, SceneProxy(InSceneProxy)
		, CurLOD(InCurLOD)
	{

	}

	const TUniformBuffer<FLandscapeComponentClusterUniformBuffer>* LandscapeComponentClusterUniformBuffer;
	const FLandscapeComponentSceneProxyInstanceMobile* SceneProxy; //#TODO: Cache RenderSystem?
	const uint32 CurLOD;
};


//Per ClusterVertexData
struct FLandscapeClusterVertex
{
	float PositionX;
	float PositionY;
};


class FLandscapeInstanceVertexFactoryMobile : public FLandscapeVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FLandscapeInstanceVertexFactoryMobile);

	typedef FLandscapeVertexFactory Super;
public:

	struct FDataType : FLandscapeVertexFactory::FDataType
	{

	};

	FLandscapeInstanceVertexFactoryMobile(ERHIFeatureLevel::Type InFeatureLevel)
		: FLandscapeVertexFactory(InFeatureLevel)
	{
	}

	virtual ~FLandscapeInstanceVertexFactoryMobile()
	{
		ReleaseResource();
	}

	/**
	* Should we cache the material's shadertype on this platform with this vertex factory?
	*/
	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);

	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NUM_VF_PACKED_INTERPOLANTS"), TEXT("1"));
	}

	// FRenderResource interface.
	virtual void InitRHI() override;

	/**
	 * An implementation of the interface used by TSynchronizedResource to update the resource with new data from the game thread.
	 */
	void SetData(const FDataType& InData)
	{
		MobileData = InData;
		UpdateRHI();
	}

private:
	/** stream component data bound to this vertex factory */
	FDataType MobileData;

	friend class FLandscapeComponentSceneProxyInstanceMobile;
};


class FLandscapeClusterVertexBuffer : public FVertexBuffer
{
public:
	static constexpr uint32 ClusterQuadSize = 64;
	static constexpr uint32 ClusterVertexDataSize = ClusterQuadSize * sizeof(FLandscapeClusterVertex);

	/** Constructor. */
	FLandscapeClusterVertexBuffer()
	{
		INC_DWORD_STAT_BY(STAT_LandscapeVertexMem, ClusterVertexDataSize);
		InitResource();
	}

	/** Destructor. */
	virtual ~FLandscapeClusterVertexBuffer()
	{
		ReleaseResource();
		//VertexMemory Calculate
		DEC_DWORD_STAT_BY(STAT_LandscapeVertexMem, ClusterVertexDataSize);
	}

	/**
	* Initialize the RHI for this rendering resource
	*/
	virtual void InitRHI() override;
};

class FLandscapeComponentSceneProxyInstanceMobile : public FLandscapeComponentSceneProxy {
private:
	struct FComponentCluster {
		FComponentCluster(const FIntPoint& InClusterBase, const FBox& InClusterBound)
			: ClusterBase(InClusterBase)
			, ClusterBound(InClusterBound)
		{}

		FIntPoint ClusterBase; 
		FBox ClusterBound;
	};


public:
	SIZE_T GetTypeHash() const override;
	FLandscapeComponentSceneProxyInstanceMobile(ULandscapeComponent* InComponent);
	virtual ~FLandscapeComponentSceneProxyInstanceMobile();
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	virtual void CreateRenderThreadResources() override;
	virtual void OnTransformChanged() override;

	TUniformBuffer<FLandscapeComponentClusterUniformBuffer> ComponentClusterUniformBuffer;

	//固定LOD等级0,先不考虑其他LOD等级
	FReadBuffer ComponentClusterBaseBuffer_GPU;
	TArray<FComponentCluster> ComponentClustersBaseAndBound_CPU;	//Box为Struct类型, 无法构建AOS结构
	TArray<FLandscapeClusterBatchElementParams> ComponentBatchUserData;

	friend class FLandscapeInstanceVertexFactoryVSParameters;
};
//@StarLight code - END LandScapeInstance, Added by yanjianhong--------------------------------------------------------------
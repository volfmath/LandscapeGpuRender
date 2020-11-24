// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
LandscapeRenderMobile.cpp: Landscape Rendering without using vertex texture fetch
=============================================================================*/

#include "LandscapeRenderMobile.h"
#include "ShaderParameterUtils.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "PrimitiveSceneInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "HAL/LowLevelMemTracker.h"
#include "MeshMaterialShader.h"
#include "Runtime/Renderer/Private/SceneCore.h"

//@StarLight code - BEGIN LandScapeInstance, Added by yanjianhong
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "LandscapeLight.h"
#include "LandscapeDataAccess.h"
//@StarLight code - END LandScapeInstance, Added by yanjianhong

// Debug CVar for disabling the loading of landscape hole meshes
static TAutoConsoleVariable<int32> CVarMobileLandscapeHoleMesh(
	TEXT("r.Mobile.LandscapeHoleMesh"),
	1,
	TEXT("Set to 0 to skip loading of landscape hole meshes on mobile."),
	ECVF_Default);

bool FLandscapeVertexFactoryMobile::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	auto FeatureLevel = GetMaxSupportedFeatureLevel(Parameters.Platform);
	return (FeatureLevel == ERHIFeatureLevel::ES3_1) &&
		(Parameters.MaterialParameters.bIsUsedWithLandscape || Parameters.MaterialParameters.bIsSpecialEngineMaterial);
}

void FLandscapeVertexFactoryMobile::InitRHI()
{
	// list of declaration items
	FVertexDeclarationElementList Elements;

	// position decls
	Elements.Add(AccessStreamComponent(MobileData.PositionComponent,0));

	if (MobileData.LODHeightsComponent.Num())
	{
		const int32 BaseAttribute = 1;
		for(int32 Index = 0;Index < MobileData.LODHeightsComponent.Num();Index++)
		{
			Elements.Add(AccessStreamComponent(MobileData.LODHeightsComponent[Index], BaseAttribute + Index));
		}
	}

	// create the actual device decls
	InitDeclaration(Elements);
}

/** Shader parameters for use with FLandscapeVertexFactory */
class FLandscapeVertexFactoryMobileVertexShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FLandscapeVertexFactoryMobileVertexShaderParameters, NonVirtual);
public:
	/**
	* Bind shader constants by name
	* @param	ParameterMap - mapping of named shader constants to indices
	*/
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		TexCoordOffsetParameter.Bind(ParameterMap,TEXT("TexCoordOffset"));
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

		const FLandscapeBatchElementParams* BatchElementParams = (const FLandscapeBatchElementParams*)BatchElement.UserData;
		check(BatchElementParams);

		const FLandscapeComponentSceneProxyMobile* SceneProxy = (const FLandscapeComponentSceneProxyMobile*)BatchElementParams->SceneProxy;
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeUniformShaderParameters>(),*BatchElementParams->LandscapeUniformShaderParametersResource);

		if (TexCoordOffsetParameter.IsBound())
		{
			FVector CameraLocalPos3D = SceneProxy->WorldToLocal.TransformPosition(InView->ViewMatrices.GetViewOrigin());

			FVector2D TexCoordOffset(
				CameraLocalPos3D.X + SceneProxy->SectionBase.X,
				CameraLocalPos3D.Y + SceneProxy->SectionBase.Y
			);
			ShaderBindings.Add(TexCoordOffsetParameter, TexCoordOffset);
		}

		if (SceneProxy->bRegistered)
		{
			ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeSectionLODUniformParameters>(), LandscapeRenderSystems.FindChecked(SceneProxy->LandscapeKey)->UniformBuffer);
		}
		else
		{
			ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeSectionLODUniformParameters>(), GNullLandscapeRenderSystemResources.UniformBuffer);
		}
			}

protected:
	LAYOUT_FIELD(FShaderParameter, TexCoordOffsetParameter);
};

/** Shader parameters for use with FLandscapeVertexFactory */
class FLandscapeVertexFactoryMobilePixelShaderParameters : public FLandscapeVertexFactoryPixelShaderParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FLandscapeVertexFactoryMobilePixelShaderParameters, NonVirtual);
public:
	/**
	* Bind shader constants by name
	* @param	ParameterMap - mapping of named shader constants to indices
	*/
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		FLandscapeVertexFactoryPixelShaderParameters::Bind(ParameterMap);
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
		SCOPE_CYCLE_COUNTER(STAT_LandscapeVFDrawTimePS);
		
		FLandscapeVertexFactoryPixelShaderParameters::GetElementShaderBindings(Scene, InView, Shader, InputStreamType, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams);
	}
};

/**
  * Shader parameters for use with FLandscapeFixedGridVertexFactory
  * Simple grid rendering (without dynamic lod blend) needs a simpler fixed setup.
  */
class FLandscapeFixedGridVertexFactoryMobileVertexShaderParameters : public FLandscapeVertexFactoryMobileVertexShaderParameters
{
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
		SCOPE_CYCLE_COUNTER(STAT_LandscapeVFDrawTimeVS);

		const FLandscapeBatchElementParams* BatchElementParams = (const FLandscapeBatchElementParams*)BatchElement.UserData;
		check(BatchElementParams);
		const FLandscapeComponentSceneProxyMobile* SceneProxy = (const FLandscapeComponentSceneProxyMobile*)BatchElementParams->SceneProxy;
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeUniformShaderParameters>(), *BatchElementParams->LandscapeUniformShaderParametersResource);
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeFixedGridUniformShaderParameters>(), SceneProxy->LandscapeFixedGridUniformShaderParameters[BatchElementParams->CurrentLOD]);

		if (TexCoordOffsetParameter.IsBound())
		{
			ShaderBindings.Add(TexCoordOffsetParameter, FVector4(ForceInitToZero));
		}
	}
};

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeVertexFactoryMobile, SF_Vertex, FLandscapeVertexFactoryMobileVertexShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeVertexFactoryMobile, SF_Pixel, FLandscapeVertexFactoryMobilePixelShaderParameters);

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeFixedGridVertexFactoryMobile, SF_Vertex, FLandscapeFixedGridVertexFactoryMobileVertexShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeFixedGridVertexFactoryMobile, SF_Pixel, FLandscapeVertexFactoryMobilePixelShaderParameters);

IMPLEMENT_VERTEX_FACTORY_TYPE(FLandscapeVertexFactoryMobile, "/Engine/Private/LandscapeVertexFactory.ush", true, true, true, false, false);
IMPLEMENT_VERTEX_FACTORY_TYPE_EX(FLandscapeFixedGridVertexFactoryMobile, "/Engine/Private/LandscapeVertexFactory.ush", true, true, true, false, false, true, false);

void FLandscapeFixedGridVertexFactoryMobile::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FLandscapeVertexFactoryMobile::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	OutEnvironment.SetDefine(TEXT("FIXED_GRID"), TEXT("1"));
}
	
bool FLandscapeFixedGridVertexFactoryMobile::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return GetMaxSupportedFeatureLevel(Parameters.Platform) == ERHIFeatureLevel::ES3_1 &&
		(Parameters.MaterialParameters.bIsUsedWithLandscape || Parameters.MaterialParameters.bIsSpecialEngineMaterial);
}

/**
* Initialize the RHI for this rendering resource
*/
void FLandscapeVertexBufferMobile::InitRHI()
{
	// create a static vertex buffer
	FRHIResourceCreateInfo CreateInfo;
	void* VertexDataPtr = nullptr;
	VertexBufferRHI = RHICreateAndLockVertexBuffer(VertexData.Num(), BUF_Static, CreateInfo, VertexDataPtr);

	// Copy stored platform data and free CPU copy
	FMemory::Memcpy(VertexDataPtr, VertexData.GetData(), VertexData.Num());
	VertexData.Empty();

	RHIUnlockVertexBuffer(VertexBufferRHI);
}

struct FLandscapeMobileHoleData
{
	FRawStaticIndexBuffer16or32Interface* IndexBuffer = nullptr;
	int32 NumHoleLods;
	int32 IndexBufferSize;
	int32 MinHoleIndex;
	int32 MaxHoleIndex;

	~FLandscapeMobileHoleData()
	{
		if (IndexBuffer != nullptr)
		{
			DEC_DWORD_STAT_BY(STAT_LandscapeHoleMem, IndexBuffer->GetResourceDataSize());
			IndexBuffer->ReleaseResource();
			delete IndexBuffer;
		}
	}
};

template <typename INDEX_TYPE>
void SerializeLandscapeMobileHoleData(FMemoryArchive& Ar, FLandscapeMobileHoleData& HoleData)
{
	Ar << HoleData.MinHoleIndex;
	Ar << HoleData.MaxHoleIndex;

	TArray<INDEX_TYPE> IndexData;
	Ar << HoleData.IndexBufferSize;
	IndexData.SetNumUninitialized(HoleData.IndexBufferSize);
	Ar.Serialize(IndexData.GetData(), HoleData.IndexBufferSize * sizeof(INDEX_TYPE));

	const bool bLoadHoleMeshData = HoleData.IndexBufferSize > 0 && CVarMobileLandscapeHoleMesh.GetValueOnGameThread();
	if (bLoadHoleMeshData)
	{
		FRawStaticIndexBuffer16or32<INDEX_TYPE>* IndexBuffer = new FRawStaticIndexBuffer16or32<INDEX_TYPE>(false);
		IndexBuffer->AssignNewBuffer(IndexData);
		HoleData.IndexBuffer = IndexBuffer;
		BeginInitResource(HoleData.IndexBuffer);
		INC_DWORD_STAT_BY(STAT_LandscapeHoleMem, HoleData.IndexBuffer->GetResourceDataSize());
	}
}

/**
 * Container for FLandscapeVertexBufferMobile that we can reference from a thread-safe shared pointer
 * while ensuring the vertex buffer is always destroyed on the render thread.
 **/
struct FLandscapeMobileRenderData
{
	FLandscapeVertexBufferMobile* VertexBuffer = nullptr;
	FLandscapeMobileHoleData* HoleData = nullptr;
	FOccluderVertexArraySP OccluderVerticesSP;

	FLandscapeMobileRenderData(const TArray<uint8>& InPlatformData)
	{
		FMemoryReader MemAr(InPlatformData);

		int32 NumHoleLods;
		MemAr << NumHoleLods;
		if (NumHoleLods > 0)
		{
			HoleData = new FLandscapeMobileHoleData;
			HoleData->NumHoleLods = NumHoleLods;

			bool b16BitIndices;
			MemAr << b16BitIndices;
			if (b16BitIndices)
			{
				SerializeLandscapeMobileHoleData<uint16>(MemAr, *HoleData);
			}
			else
			{
				SerializeLandscapeMobileHoleData<uint32>(MemAr, *HoleData);
			}
		}

		{
			int32 VertexCount;
			MemAr << VertexCount;
			TArray<uint8> VertexData;
			VertexData.SetNumUninitialized(VertexCount*sizeof(FLandscapeMobileVertex));
			MemAr.Serialize(VertexData.GetData(), VertexData.Num());
			VertexBuffer = new FLandscapeVertexBufferMobile(MoveTemp(VertexData));
		}
		
		{
			int32 NumOccluderVertices;
			MemAr << NumOccluderVertices;
			if (NumOccluderVertices > 0)
			{
				OccluderVerticesSP = MakeShared<FOccluderVertexArray, ESPMode::ThreadSafe>();
				OccluderVerticesSP->SetNumUninitialized(NumOccluderVertices);
				MemAr.Serialize(OccluderVerticesSP->GetData(), NumOccluderVertices * sizeof(FVector));

				INC_DWORD_STAT_BY(STAT_LandscapeOccluderMem, OccluderVerticesSP->GetAllocatedSize());
			}
		}
	}

	~FLandscapeMobileRenderData()
	{
		// Make sure the vertex buffer is always destroyed from the render thread 
		if (VertexBuffer != nullptr)
		{
			if (IsInRenderingThread())
			{
				delete VertexBuffer;
				delete HoleData;
			}
			else
			{
				FLandscapeVertexBufferMobile* InVertexBuffer = VertexBuffer;
				FLandscapeMobileHoleData* InHoleData = HoleData;
				ENQUEUE_RENDER_COMMAND(InitCommand)(
					[InVertexBuffer, InHoleData](FRHICommandListImmediate& RHICmdList)
				{
					delete InVertexBuffer;
					delete InHoleData;
				});
			}
		}

		if (OccluderVerticesSP.IsValid())
		{
			DEC_DWORD_STAT_BY(STAT_LandscapeOccluderMem, OccluderVerticesSP->GetAllocatedSize());
		}
	}
};

FLandscapeComponentSceneProxyMobile::FLandscapeComponentSceneProxyMobile(ULandscapeComponent* InComponent)
	: FLandscapeComponentSceneProxy(InComponent)
	, MobileRenderData(InComponent->PlatformData.GetRenderData())
{
	check(InComponent);
	
	check(InComponent->MobileMaterialInterfaces.Num() > 0);
	check(InComponent->MobileWeightmapTextures.Num() > 0);

	WeightmapTextures = InComponent->MobileWeightmapTextures;
	NormalmapTexture = InComponent->MobileWeightmapTextures[0];

#if WITH_EDITOR
	TArray<FWeightmapLayerAllocationInfo>& LayerAllocations = InComponent->MobileWeightmapLayerAllocations.Num() ? InComponent->MobileWeightmapLayerAllocations : InComponent->GetWeightmapLayerAllocations();
	LayerColors.Empty();
	for (auto& Allocation : LayerAllocations)
	{
		if (Allocation.LayerInfo != nullptr)
		{
			LayerColors.Add(Allocation.LayerInfo->LayerUsageDebugColor);
		}
	}
#endif
}

int32 FLandscapeComponentSceneProxyMobile::CollectOccluderElements(FOccluderElementsCollector& Collector) const
{
	if (MobileRenderData->OccluderVerticesSP.IsValid() && SharedBuffers->OccluderIndicesSP.IsValid())
	{
		Collector.AddElements(MobileRenderData->OccluderVerticesSP, SharedBuffers->OccluderIndicesSP, GetLocalToWorld());
		return 1;
	}

	return 0;
}

FLandscapeComponentSceneProxyMobile::~FLandscapeComponentSceneProxyMobile()
{
	if (VertexFactory)
	{
		delete VertexFactory;
		VertexFactory = NULL;
	}
}

SIZE_T FLandscapeComponentSceneProxyMobile::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FLandscapeComponentSceneProxyMobile::CreateRenderThreadResources()
{
	LLM_SCOPE(ELLMTag::Landscape);

	if (IsComponentLevelVisible())
	{
		RegisterNeighbors(this);
	}
	
	auto FeatureLevel = GetScene().GetFeatureLevel();
	
	// Use only index buffers from the shared buffers since the vertex buffers are unique per proxy on mobile
	SharedBuffers = FLandscapeComponentSceneProxy::SharedBuffersMap.FindRef(SharedBuffersKey);
	if (SharedBuffers == nullptr)
	{
		int32 NumOcclusionVertices = MobileRenderData->OccluderVerticesSP.IsValid() ? MobileRenderData->OccluderVerticesSP->Num() : 0;
				
		SharedBuffers = new FLandscapeSharedBuffers(
			SharedBuffersKey, SubsectionSizeQuads, NumSubsections,
			GetScene().GetFeatureLevel(), false, NumOcclusionVertices);

		FLandscapeComponentSceneProxy::SharedBuffersMap.Add(SharedBuffersKey, SharedBuffers);
	}
	SharedBuffers->AddRef();

	// Init vertex buffer
	{
		check(MobileRenderData->VertexBuffer);
		MobileRenderData->VertexBuffer->InitResource();

		FLandscapeVertexFactoryMobile* LandscapeVertexFactory = new FLandscapeVertexFactoryMobile(FeatureLevel);
		LandscapeVertexFactory->MobileData.PositionComponent = FVertexStreamComponent(MobileRenderData->VertexBuffer, STRUCT_OFFSET(FLandscapeMobileVertex, Position), sizeof(FLandscapeMobileVertex), VET_UByte4N);
		for (uint32 Index = 0; Index < LANDSCAPE_MAX_ES_LOD_COMP; ++Index)
		{
			LandscapeVertexFactory->MobileData.LODHeightsComponent.Add
			(FVertexStreamComponent(MobileRenderData->VertexBuffer, STRUCT_OFFSET(FLandscapeMobileVertex, LODHeights) + sizeof(uint8) * 4 * Index, sizeof(FLandscapeMobileVertex), VET_UByte4N));
		}

		LandscapeVertexFactory->InitResource();
		VertexFactory = LandscapeVertexFactory;
	}

	// Init vertex buffer for rendering to virtual texture
	if (UseVirtualTexturing(FeatureLevel))
	{
		FLandscapeFixedGridVertexFactoryMobile* LandscapeVertexFactory = new FLandscapeFixedGridVertexFactoryMobile(FeatureLevel);
		LandscapeVertexFactory->MobileData.PositionComponent = FVertexStreamComponent(MobileRenderData->VertexBuffer, STRUCT_OFFSET(FLandscapeMobileVertex, Position), sizeof(FLandscapeMobileVertex), VET_UByte4N);
		
		for (uint32 Index = 0; Index < LANDSCAPE_MAX_ES_LOD_COMP; ++Index)
		{
			LandscapeVertexFactory->MobileData.LODHeightsComponent.Add
			(FVertexStreamComponent(MobileRenderData->VertexBuffer, STRUCT_OFFSET(FLandscapeMobileVertex, LODHeights) + sizeof(uint8) * 4 * Index, sizeof(FLandscapeMobileVertex), VET_UByte4N));
		}
		
		LandscapeVertexFactory->InitResource();
		FixedGridVertexFactory = LandscapeVertexFactory;
	}

	// Assign LandscapeUniformShaderParameters
	LandscapeUniformShaderParameters.InitResource();

	// Create per Lod uniform buffers
	LandscapeFixedGridUniformShaderParameters.AddDefaulted(MaxLOD + 1);
	for (int32 LodIndex = 0; LodIndex <= MaxLOD; ++LodIndex)
	{
		LandscapeFixedGridUniformShaderParameters[LodIndex].InitResource();
		FLandscapeFixedGridUniformShaderParameters Parameters;
		Parameters.LodValues = FVector4(
			LodIndex,
			0.f,
			(float)((SubsectionSizeVerts >> LodIndex) - 1),
			1.f / (float)((SubsectionSizeVerts >> LodIndex) - 1));
		LandscapeFixedGridUniformShaderParameters[LodIndex].SetContents(Parameters);
	}
}

TSharedPtr<FLandscapeMobileRenderData, ESPMode::ThreadSafe> FLandscapeComponentDerivedData::GetRenderData()
{
	// This function is expected to be called from either the GameThread or via ParallelFor from the GameThread
	check(!IsInRenderingThread());

	if (FPlatformProperties::RequiresCookedData() && CachedRenderData.IsValid())
	{
		// on device we can re-use the cached data if we are re-registering our component.
		return CachedRenderData;
	}
	else
	{
		check(CompressedLandscapeData.Num() > 0);

		FMemoryReader Ar(CompressedLandscapeData);

		// Note: change LANDSCAPE_FULL_DERIVEDDATA_VER when modifying the serialization layout
		int32 UncompressedSize;
		Ar << UncompressedSize;

		int32 CompressedSize;
		Ar << CompressedSize;

		TArray<uint8> CompressedData;
		CompressedData.Empty(CompressedSize);
		CompressedData.AddUninitialized(CompressedSize);
		Ar.Serialize(CompressedData.GetData(), CompressedSize);

		TArray<uint8> UncompressedData;
		UncompressedData.Empty(UncompressedSize);
		UncompressedData.AddUninitialized(UncompressedSize);

		verify(FCompression::UncompressMemory(NAME_Zlib, UncompressedData.GetData(), UncompressedSize, CompressedData.GetData(), CompressedSize));

		TSharedPtr<FLandscapeMobileRenderData, ESPMode::ThreadSafe> RenderData = MakeShareable(new FLandscapeMobileRenderData(MoveTemp(UncompressedData)));

		// if running on device		
		if (FPlatformProperties::RequiresCookedData())
		{
			// free the compressed data now that we have used it to create the render data.
			CompressedLandscapeData.Empty();
			// store a reference to the render data so we can use it again should the component be reregistered.
			CachedRenderData = RenderData;
		}

		return RenderData;
	}
}

void FLandscapeComponentSceneProxyMobile::ApplyMeshElementModifier(FMeshBatchElement& InOutMeshElement, int32 InLodIndex) const
{
	const bool bHoleDataExists = MobileRenderData->HoleData != nullptr && MobileRenderData->HoleData->IndexBuffer != nullptr && InLodIndex < MobileRenderData->HoleData->NumHoleLods;
	if (bHoleDataExists)
	{
		FLandscapeMobileHoleData const& HoleData = *MobileRenderData->HoleData;
		InOutMeshElement.IndexBuffer = HoleData.IndexBuffer;
		InOutMeshElement.NumPrimitives = HoleData.IndexBufferSize / 3;
		InOutMeshElement.FirstIndex = 0;
		InOutMeshElement.MinVertexIndex = HoleData.MinHoleIndex;
		InOutMeshElement.MaxVertexIndex = HoleData.MaxHoleIndex;
	}
}

//@StarLight code - BEGIN Optimize terrain LOD, Added by zhuyule
int32 GLandscapeOptimizeLOD = 0;
FAutoConsoleVariableRef CVarLandscapeOptimizeLOD(
	TEXT("r.LandscapeOptimizeLOD"),
	GLandscapeOptimizeLOD,
	TEXT("Optimize LOD for landscape/terrain meshes."),
	ECVF_Scalability
);

bool FLandscapeComponentSceneProxyMobile::IsUsingCustomLODRules() const
{ 
	return GLandscapeOptimizeLOD > 0; 
}

//FLODMask FLandscapeComponentSceneProxyMobile::GetCustomLOD(const FSceneView& InView, float InViewLODScale, int32 InForcedLODLevel, float& OutScreenSizeSquared) const
//{
//	FLODMask LODToRender;
//	//const FSceneView& LODView = GetLODView(InView);
//
//	//const int32 NumMeshes = GetPrimitiveSceneInfo()->StaticMeshRelevances.Num();
//
//	//int32 MinLODFound = INT_MAX;
//	//bool bFoundLOD = false;
//	//OutScreenSizeSquared = ComputeBoundsScreenSquared(GetBounds(), InView.ViewMatrices.GetViewOrigin(), LODView);
//
//	//for (int32 MeshIndex = NumMeshes - 1; MeshIndex >= 0; --MeshIndex)
//	//{
//	//	const FStaticMeshBatchRelevance& Mesh = GetPrimitiveSceneInfo()->StaticMeshRelevances[MeshIndex];
//
//	//	float MeshScreenSize = Mesh.ScreenSize;
//
//	//	if (FMath::Square(MeshScreenSize * 0.5f) >= OutScreenSizeSquared)
//	//	{
//	//		LODToRender.SetLOD(Mesh.LODIndex);
//	//		bFoundLOD = true;
//	//		break;
//	//	}
//
//	//	MinLODFound = FMath::Min<int32>(MinLODFound, Mesh.LODIndex);
//	//}
//	//// If no LOD was found matching the screen size, use the lowest in the array instead of LOD 0, to handle non-zero MinLOD
//	//if (!bFoundLOD)
//	//{
//	//	LODToRender.SetLOD(MinLODFound);
//	//}
//
//	return LODToRender;
//}
//@StarLight code - END Optimize terrain LOD, Added by zhuyule



//@StarLight code - BEGIN LandScapeInstance, Added by yanjianhong----------------------------------------------------
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FLandscapeComponentClusterUniformBuffer, "ComponentClusterParameters");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FLandscapeClusterLODUniformBuffer, "GlobalClusterParameters");


class FLandscapeInstanceVertexFactoryVSParameters : public FVertexFactoryShaderParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FLandscapeInstanceVertexFactoryVSParameters, NonVirtual);
public:
	/**
	* Bind shader constants by name
	* @param	ParameterMap - mapping of named shader constants to indices
	*/
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		TexCoordOffsetParameter.Bind(ParameterMap, TEXT("TexCoordOffset"));
		ComponentClusterBaseBuffer.Bind(ParameterMap, TEXT("ComponentClusterBaseBuffer"));
		ClusterLODBuffer.Bind(ParameterMap, TEXT("ClusterLODBuffer"));
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

		//检查有地方用到?
		check(!TexCoordOffsetParameter.IsBound());

		const FLandscapeClusterBatchElementParams* BatchElementParams = (const FLandscapeClusterBatchElementParams*)BatchElement.UserData;

		const FLandscapeComponentSceneProxyInstanceMobile* SceneProxy = BatchElementParams->SceneProxy;
		FLandscapeRenderSystem* RenderSystem = LandscapeRenderSystems.FindChecked(SceneProxy->LandscapeKey);

		//UniformBuffer都是MutilFrame Resource
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeComponentClusterUniformBuffer>(), *BatchElementParams->LandscapeComponentClusterUniformBuffer);
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeClusterLODUniformBuffer>(), RenderSystem->ClusterLodUniformBuffer);

		ShaderBindings.Add(ComponentClusterBaseBuffer, SceneProxy->ComponentClusterBaseBuffer_GPU.SRV);
		ShaderBindings.Add(ClusterLODBuffer, RenderSystem->ClusterLODValues_GPU.SRV);
	}

protected:
	LAYOUT_FIELD(FShaderParameter, TexCoordOffsetParameter);
	LAYOUT_FIELD(FShaderResourceParameter, ComponentClusterBaseBuffer)
	LAYOUT_FIELD(FShaderResourceParameter, ClusterLODBuffer)
};

class FLandscapeInstanceVertexFactoryPSParameters : public FVertexFactoryShaderParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FLandscapeInstanceVertexFactoryPSParameters, NonVirtual);
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

		const FLandscapeClusterBatchElementParams* BatchElementParams = (const FLandscapeClusterBatchElementParams*)BatchElement.UserData;

		ShaderBindings.Add(Shader->GetUniformBufferParameter<FLandscapeComponentClusterUniformBuffer>(), *BatchElementParams->LandscapeComponentClusterUniformBuffer);
	}
};


IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeInstanceVertexFactoryMobile, SF_Vertex, FLandscapeInstanceVertexFactoryVSParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FLandscapeInstanceVertexFactoryMobile, SF_Pixel, FLandscapeInstanceVertexFactoryPSParameters);
IMPLEMENT_VERTEX_FACTORY_TYPE(FLandscapeInstanceVertexFactoryMobile, "/Engine/Private/LandscapeVertexFactory.ush", true, true, true, false, false);


void FLandscapeClusterVertexBuffer::InitRHI() {

	SCOPED_LOADTIMER(FLandscapeClusterVertexBuffer_InitRHI);

	// create a static vertex buffer
	uint32 VertexSize = ClusterQuadSize + 1;
	FRHIResourceCreateInfo CreateInfo;
	void* BufferData = nullptr;
	VertexBufferRHI = RHICreateAndLockVertexBuffer(VertexSize * VertexSize * sizeof(FLandscapeClusterVertex), BUF_Static, CreateInfo, BufferData);
	FLandscapeClusterVertex* Vertex = reinterpret_cast<FLandscapeClusterVertex*>(BufferData);

	for (uint32 y = 0; y < VertexSize; y++){
		for (uint32 x = 0; x < VertexSize; x++){
			Vertex->PositionX = static_cast<float>(x);
			Vertex->PositionY = static_cast<float>(y);
			Vertex++;
		}
	}
	RHIUnlockVertexBuffer(VertexBufferRHI);
}


FLandscapeComponentSceneProxyInstanceMobile::FLandscapeComponentSceneProxyInstanceMobile(ULandscapeComponent* InComponent)
	: FLandscapeComponentSceneProxy(InComponent)
{
	check(InComponent);

	check(InComponent->MobileMaterialInterfaces.Num() > 0);
	check(InComponent->MobileWeightmapTextures.Num() > 0);
//
	WeightmapTextures = InComponent->MobileWeightmapTextures;

	//#TODO: 暂时使用WeightMap, 待替换为HeightMap
	NormalmapTexture = InComponent->MobileWeightmapTextures[0];

}

FLandscapeComponentSceneProxyInstanceMobile::~FLandscapeComponentSceneProxyInstanceMobile() {
	ComponentClusterUniformBuffer.ReleaseResource();
	ComponentClusterBaseBuffer_GPU.Release();
}

SIZE_T FLandscapeComponentSceneProxyInstanceMobile::GetTypeHash() const {
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FLandscapeComponentSceneProxyInstanceMobile::CreateRenderThreadResources() {

	check(HeightmapTexture != nullptr);

	//memory tracker
	LLM_SCOPE(ELLMTag::Landscape); 

	if (IsComponentLevelVisible()){
		//#TODO: 大部分内容不再需要
		RegisterNeighbors(this); 
	}

	auto FeatureLevel = GetScene().GetFeatureLevel();

	//使用同样的VertexBuffer和IndexBuffer, 不再是每个SceneProxy单独一份，其中让SharedBuffer创建ClusterIndexBuffer和ClusterVertexBuffer
	//Initial VertexFactory, ClusterVertexBuffer and ClusterIndexBuffer has been created in sharedBuffers
	SharedBuffers = FLandscapeComponentSceneProxy::SharedBuffersMap.FindRef(SharedBuffersKey);
	if (SharedBuffers == nullptr) {
		//don't need SOC data
		SharedBuffers = new FLandscapeSharedBuffers(SharedBuffersKey, SubsectionSizeQuads, NumSubsections, FeatureLevel, false, /*NumOcclusionVertices*/0);
		FLandscapeComponentSceneProxy::SharedBuffersMap.Add(SharedBuffersKey, SharedBuffers);

		check(SharedBuffers->ClusterVertexBuffer);
		FLandscapeInstanceVertexFactoryMobile* LandscapeVertexFactory = new FLandscapeInstanceVertexFactoryMobile(FeatureLevel);
		LandscapeVertexFactory->MobileData.PositionComponent = FVertexStreamComponent(SharedBuffers->ClusterVertexBuffer, 0, sizeof(FLandscapeClusterVertex), VET_Float2);
		LandscapeVertexFactory->InitResource();

		//不初始化SceneProxy中的VertexFactory, 资源生命周期均由SharedBuffers管理
		SharedBuffers->ClusterVertexFactory = LandscapeVertexFactory;
	}
	SharedBuffers->AddRef();


	//初始化UniformBuffer和BatchParameter
	{
		ComponentClusterUniformBuffer.InitResource(); //Content和GPUBuffer在内部是分开的(兼容Descript?)
		ComponentBatchUserData.Empty(SharedBuffers->NumClusterLOD);
		for (uint32 Lod = 0; Lod < SharedBuffers->NumClusterLOD; ++Lod) {
			ComponentBatchUserData.Emplace(&ComponentClusterUniformBuffer, this, Lod);
		}
	}


	//初始化ClusterBaseCPUBuffer, ClusterBaseGPUBuffer
	{
		uint32 IdleStart = FPlatformTime::Cycles();

		check(ComponentSizeQuads + NumSubsections == 512);
		check(ComponentSizeQuads + NumSubsections >= FLandscapeClusterVertexBuffer::ClusterQuadSize);

		uint32 ComponentClusterSize = (ComponentSizeQuads + NumSubsections) / FLandscapeClusterVertexBuffer::ClusterQuadSize;
		ClusterBases_CPU.Empty(ComponentClusterSize * ComponentClusterSize);
		ClusterBounds_CPU.Empty(ComponentClusterSize * ComponentClusterSize);
		//当前Component仅仅知道二维坐标，但无法计算全局LinearIndex
		FIntPoint ClusterComponentBase = ComponentBase * ComponentClusterSize;

		for (uint32 y = 0; y < ComponentClusterSize; ++y) {
			for (uint32 x = 0; x < ComponentClusterSize; ++x) {
				ClusterBounds_CPU.Emplace(CalcClusterBounds(ClusterComponentBase.X + x, ClusterComponentBase.Y + y));

				//#TODO:添加顶点xy最大大小
				ClusterBases_CPU.Emplace(ClusterComponentBase.X + x, ClusterComponentBase.Y + y);
			}
		}

		//Debug Only
		FLandscapeRenderSystem* RenderSystem = LandscapeRenderSystems.FindChecked(LandscapeKey);
		check(RenderSystem->PerComponentClusterSize * RenderSystem->PerComponentClusterSize == ClusterBases_CPU.Num());

		//#TODO: Use PF_R16G16_UINT or PF_R8_UINT
		ComponentClusterBaseBuffer_GPU.Initialize(8, ClusterBases_CPU.Num(), PF_R32G32_UINT, BUF_Dynamic);


		UE_LOG(LogTemp, Log, TEXT("Initial ClusterBase And Bounds: %u"), FPlatformTime::ToMilliseconds(FPlatformTime::Cycles() - IdleStart));
	}

}


TArray<FBox> Test;
void FLandscapeComponentSceneProxyInstanceMobile::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const{

	{
		SCOPE_CYCLE_COUNTER(STAT_LandscapeClusterInitialBaseAndBounds);
		uint32 ComponentClusterSize = (ComponentSizeQuads + NumSubsections) / FLandscapeClusterVertexBuffer::ClusterQuadSize;
		Test.Empty(ComponentClusterSize * ComponentClusterSize);
		//当前Component仅仅知道二维坐标，但无法计算全局LinearIndex
		FIntPoint ClusterComponentBase = ComponentBase * ComponentClusterSize;

		for (uint32 y = 0; y < ComponentClusterSize; ++y) {
			for (uint32 x = 0; x < ComponentClusterSize; ++x) {
				Test.Emplace(CalcClusterBounds(ClusterComponentBase.X + x, ClusterComponentBase.Y + y));
			}
		}
	}



	//QUICK_SCOPE_CYCLE_COUNTER(STAT_FLandscapeComponentSceneProxyInstance_GetMeshElements);
	SCOPE_CYCLE_COUNTER(STAT_LandscapeDynamicDrawTime);

	check(Views.Num() == 1);
	const FSceneView* View = Views[0];
	FMeshBatch& MeshBatch = Collector.AllocateMesh();

	uint32 LODIndex = 0; //#TODO: 计算LOD
	uint32 CurClusterQuadSize = FLandscapeClusterVertexBuffer::ClusterQuadSize >> LODIndex;
	uint32 CurClusterVertexSize = CurClusterQuadSize + 1;
/*	NumTriangles += Mesh.GetNumPrimitives();
	NumDrawCalls += Mesh.Elements.Num()*/;

	UMaterialInterface* MaterialInterface = nullptr;

	{
		//#TODO: 使用LOD材质?
		int32 MaterialIndex = LODIndexToMaterialIndex[LODIndex];
		MaterialInterface = AvailableMaterials[MaterialIndex];
		check(MaterialInterface != nullptr);
	}

	{
		check(bRegistered);

		MeshBatch.VertexFactory = SharedBuffers->ClusterVertexFactory;
		MeshBatch.MaterialRenderProxy = MaterialInterface->GetRenderProxy();

		MeshBatch.LCI = ComponentLightInfo.Get();
		MeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();
		MeshBatch.CastShadow = true;
		MeshBatch.bUseForDepthPass = true;
		MeshBatch.bUseAsOccluder = false; //
		MeshBatch.bUseForMaterial = true;
		MeshBatch.Type = PT_TriangleList;
		MeshBatch.DepthPriorityGroup = SDPG_World;
		MeshBatch.LODIndex = LODIndex; 
		MeshBatch.bDitheredLODTransition = false;

		// Combined batch element
		FMeshBatchElement& BatchElement = MeshBatch.Elements[0];

		BatchElement.UserData = &ComponentBatchUserData[LODIndex];
		BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
		BatchElement.IndexBuffer = SharedBuffers->ClusterIndexBuffers[LODIndex];
		BatchElement.NumPrimitives = CurClusterQuadSize * CurClusterQuadSize * 2u;
		BatchElement.FirstIndex = 0;
		BatchElement.MinVertexIndex = 0;
		BatchElement.MaxVertexIndex = CurClusterVertexSize * CurClusterVertexSize - 1;
		BatchElement.NumInstances = ClusterBases_CPU.Num();

		//不需要考虑Hole
		//ApplyMeshElementModifier(BatchElement, LODIndex);
	}

	MeshBatch.bCanApplyViewModeOverrides = true; //兼容WireFrame等
	MeshBatch.bUseWireframeSelectionColoring = IsSelected(); //选中颜色
	Collector.AddMesh(0, MeshBatch);

	//#TODO: 
	{
		void* Data = RHILockVertexBuffer(ComponentClusterBaseBuffer_GPU.Buffer, 0, sizeof(FIntPoint) * ClusterBases_CPU.Num(), RLM_WriteOnly);
		FMemory::Memcpy(Data, ClusterBases_CPU.GetData(), sizeof(FIntPoint) * ClusterBases_CPU.Num());
		RHIUnlockVertexBuffer(ComponentClusterBaseBuffer_GPU.Buffer);
	}


	if (ViewFamily.EngineShowFlags.Bounds)
	{	
		for (const auto& CurBound : ClusterBounds_CPU) {
			RenderOnlyBox(Collector.GetPDI(0), CurBound);
		}	
	}
}

void FLandscapeComponentSceneProxyInstanceMobile::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) 
{
	if (AvailableMaterials.Num() == 0)
	{
		return;
	}

	//暂时不支持VT
	check(RuntimeVirtualTextureMaterialTypes.Num() == 0);

	int32 TotalBatchCount = 1 + LastLOD - FirstLOD;
	TotalBatchCount += (1 + LastVirtualTextureLOD - FirstVirtualTextureLOD) * RuntimeVirtualTextureMaterialTypes.Num();

	StaticBatchParamArray.Empty(TotalBatchCount);
	PDI->ReserveMemoryForMeshes(TotalBatchCount);

	// Add fixed grid mesh batches for runtime virtual texture usage
	for (ERuntimeVirtualTextureMaterialType MaterialType : RuntimeVirtualTextureMaterialTypes)
	{
		const int32 MaterialIndex = LODIndexToMaterialIndex[FirstLOD];

		for (int32 LODIndex = FirstVirtualTextureLOD; LODIndex <= LastVirtualTextureLOD; ++LODIndex)
		{
			FMeshBatch RuntimeVirtualTextureMeshBatch;
			if (GetMeshElementForVirtualTexture(LODIndex, MaterialType, AvailableMaterials[MaterialIndex], RuntimeVirtualTextureMeshBatch, StaticBatchParamArray))
			{
				PDI->DrawMesh(RuntimeVirtualTextureMeshBatch, FLT_MAX);
			}
		}
	}
}


void FLandscapeComponentSceneProxyInstanceMobile::OnTransformChanged() {

	// Set Lightmap ScaleBias
	int32 PatchExpandCountX = 0;
	int32 PatchExpandCountY = 0;
	int32 DesiredSize = 1; // output by GetTerrainExpandPatchCount but not used below
	const float LightMapRatio = ::GetTerrainExpandPatchCount(StaticLightingResolution, PatchExpandCountX, PatchExpandCountY, ComponentSizeQuads, (NumSubsections * (SubsectionSizeQuads + 1)), DesiredSize, StaticLightingLOD);
	const float LightmapLODScaleX = LightMapRatio / ((ComponentSizeVerts >> StaticLightingLOD) + 2 * PatchExpandCountX);
	const float LightmapLODScaleY = LightMapRatio / ((ComponentSizeVerts >> StaticLightingLOD) + 2 * PatchExpandCountY);
	const float LightmapBiasX = PatchExpandCountX * LightmapLODScaleX;
	const float LightmapBiasY = PatchExpandCountY * LightmapLODScaleY;
	const float LightmapScaleX = LightmapLODScaleX * (float)((ComponentSizeVerts >> StaticLightingLOD) - 1) / ComponentSizeQuads;
	const float LightmapScaleY = LightmapLODScaleY * (float)((ComponentSizeVerts >> StaticLightingLOD) - 1) / ComponentSizeQuads;
	const float LightmapExtendFactorX = (float)SubsectionSizeQuads * LightmapScaleX;
	const float LightmapExtendFactorY = (float)SubsectionSizeQuads * LightmapScaleY;

	// cache component's WorldToLocal
	FMatrix LtoW = GetLocalToWorld();
	WorldToLocal = LtoW.Inverse();

	// cache component's LocalToWorldNoScaling
	LocalToWorldNoScaling = LtoW;
	LocalToWorldNoScaling.RemoveScaling();

	// Set FLandscapeUniformVSParameters for this Component
	FLandscapeComponentClusterUniformBuffer LandscapeClusterParams;

	LandscapeClusterParams.HeightmapUVScaleBias = HeightmapScaleBias;
	LandscapeClusterParams.WeightmapUVScaleBias = WeightmapScaleBias;
	LandscapeClusterParams.LocalToWorldNoScaling = LocalToWorldNoScaling;

	LandscapeClusterParams.LandscapeLightmapScaleBias = FVector4(
		LightmapScaleX,
		LightmapScaleY,
		LightmapBiasY,
		LightmapBiasX);

	LandscapeClusterParams.SubsectionSizeVertsLayerUVPan = FVector4(
		SubsectionSizeVerts,
		1.f / (float)SubsectionSizeQuads,
		SectionBase.X,
		SectionBase.Y
	);

	LandscapeClusterParams.SubsectionOffsetParams = FVector4(
		HeightmapSubsectionOffsetU,
		HeightmapSubsectionOffsetV,
		WeightmapSubsectionOffset,
		SubsectionSizeQuads
	);

	LandscapeClusterParams.LightmapSubsectionOffsetParams = FVector4(
		LightmapExtendFactorX,
		LightmapExtendFactorY,
		0,
		0
	);

	LandscapeClusterParams.BlendableLayerMask = FVector4(
		BlendableLayerMask & (1 << 0) ? 1 : 0,
		BlendableLayerMask & (1 << 1) ? 1 : 0,
		BlendableLayerMask & (1 << 2) ? 1 : 0,
		0
	);


	LandscapeClusterParams.HeightmapTexture = HeightmapTexture->TextureReference.TextureReferenceRHI;
	LandscapeClusterParams.HeightmapTextureSampler = TStaticSamplerState<SF_Point>::GetRHI();


	check(XYOffsetmapTexture == nullptr);

	if (NormalmapTexture)
	{
		LandscapeClusterParams.NormalmapTexture = NormalmapTexture->TextureReference.TextureReferenceRHI;
		LandscapeClusterParams.NormalmapTextureSampler = NormalmapTexture->Resource->SamplerStateRHI;
	}
	else
	{
		LandscapeClusterParams.NormalmapTexture = GBlackTexture->TextureRHI;
		LandscapeClusterParams.NormalmapTextureSampler = GBlackTexture->SamplerStateRHI;
	}

	ComponentClusterUniformBuffer.SetContents(LandscapeClusterParams);

	//仅初始化时创建,不可能被注册
	check(!bRegistered);

	//使用Dynamic渲染, 不用更新PrimitivesNeedingStaticMeshUpdate容器, 即不会重新CacheDrawCommand
	// Recache mesh draw commands for changed uniform buffers
	//GetScene().UpdateCachedRenderStates(this);
}


FBox FLandscapeComponentSceneProxyInstanceMobile::CalcClusterBounds(uint32 ClusterBaseX, uint32 ClusterBaseY) {
	
	FBox LocalBox(ForceInit);

	uint32 HeightmapSizeX = HeightmapTexture->Source.GetSizeX();
	uint32 HeightmapSizeY = HeightmapTexture->Source.GetSizeY();
	check(HeightmapSizeX == HeightmapSizeY);

	//一张高度图对应一个Component
	check(HeightmapScaleBias.Z == 0.f && HeightmapScaleBias.W == 0.f);

	//因为按2的幂数划Cluster, 必然有顶点超出, 每个Cluster的起始顶点都是重合的
	//Section接缝处的Cluster, 采样高度图要进行偏移，Section处相邻的Height值相同
	uint32 StartX = ClusterBaseX * FLandscapeClusterVertexBuffer::ClusterQuadSize;
	uint32 EndX = FMath::Min(StartX + FLandscapeClusterVertexBuffer::ClusterQuadSize, static_cast<uint32>(ComponentSizeQuads));
	check(HeightmapSizeX > EndX);


	uint32 StartY = ClusterBaseY * FLandscapeClusterVertexBuffer::ClusterQuadSize;
	uint32 EndY = FMath::Min(StartY + FLandscapeClusterVertexBuffer::ClusterQuadSize, static_cast<uint32>(ComponentSizeQuads));
	check(HeightmapSizeY > EndY);
	
	FColor* HeightMapData = reinterpret_cast<FColor*>(HeightmapTexture->Source.LockMip(0));

	//第SubsectionSizeVerts + 1个顶点,采样要偏移
	for (uint32 y = StartY; y <= EndY; ++y) {
		uint32 SampleTexelY = SubsectionSizeVerts  == y ? y + 1 : y;

		for (uint32 x = StartX; x <= EndX; ++x) {			
			uint32 SampleTexelX = SubsectionSizeVerts  == x ? x + 1 : x;		
			uint32 HeightMapIndex = HeightmapSizeX * SampleTexelY + SampleTexelX;
			
			const auto& HeightValue = HeightMapData[HeightMapIndex];

			float VertexHeight = LandscapeDataAccess::GetLocalHeight(static_cast<uint16>(HeightValue.R << 8u | HeightValue.G));
			const FVector LocalVertex(x, y, VertexHeight);
			LocalBox += LocalVertex;
		}
	}
	HeightmapTexture->Source.UnlockMip(0);
	auto WorldBox = LocalBox.TransformBy(GetLocalToWorld());
	return WorldBox;
}

void FLandscapeComponentSceneProxyInstanceMobile::RenderOnlyBox(FPrimitiveDrawInterface* PDI, const FBoxSphereBounds& InBounds) const{
	// Draw the static mesh's bounding box and sphere.
	const ESceneDepthPriorityGroup DrawBoundsDPG = SDPG_World;
	DrawWireBox(PDI, InBounds.GetBox(), FColor(72, 72, 255), DrawBoundsDPG);
	/*DrawCircle(PDI, InBounds.Origin, FVector(1, 0, 0), FVector(0, 1, 0), FColor::Yellow, InBounds.SphereRadius, 32, DrawBoundsDPG);
	DrawCircle(PDI, InBounds.Origin, FVector(1, 0, 0), FVector(0, 0, 1), FColor::Yellow, InBounds.SphereRadius, 32, DrawBoundsDPG);
	DrawCircle(PDI, InBounds.Origin, FVector(0, 1, 0), FVector(0, 0, 1), FColor::Yellow, InBounds.SphereRadius, 32, DrawBoundsDPG);*/
}

bool FLandscapeInstanceVertexFactoryMobile::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return GetMaxSupportedFeatureLevel(Parameters.Platform) == ERHIFeatureLevel::ES3_1 &&
		(Parameters.MaterialParameters.bIsUsedWithLandscape || Parameters.MaterialParameters.bIsSpecialEngineMaterial);
}


void FLandscapeInstanceVertexFactoryMobile::InitRHI()
{
	// list of declaration items
	FVertexDeclarationElementList Elements;

	// position decls
	Elements.Add(AccessStreamComponent(MobileData.PositionComponent, 0));

	// create the actual device decls
	InitDeclaration(Elements);
}


//@StarLight code - END LandScapeInstance, Added by yanjianhong

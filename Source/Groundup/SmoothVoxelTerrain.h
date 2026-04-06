#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMeshOverlay.h"
#include "SmoothVoxelTerrain.generated.h"

namespace UE::Geometry { class FDynamicMesh3; }

UENUM(BlueprintType)
enum class EVoxelType : uint8
{
    Air   UMETA(DisplayName = "Air"),
    Grass UMETA(DisplayName = "Grass"),
    Dirt  UMETA(DisplayName = "Dirt"),
    Stone UMETA(DisplayName = "Stone")
};

UCLASS()
class GROUNDUP_API ASmoothVoxelTerrain : public AActor
{
    GENERATED_BODY()

public:
    ASmoothVoxelTerrain();
    ~ASmoothVoxelTerrain();

protected:
    virtual void BeginPlay() override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;


public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    UMaterialInterface* TerrainMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    int32 ChunkSize = 32;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    int32 MaxHeight = 64;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    int32 WorldChunksX = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    int32 WorldChunksY = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    float CubeSize = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    float NoiseScale = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    float HeightMultiplier = 2000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    float MinGrassThickness = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    int32 Seed = 1337;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    bool bSmoothTerrain = false;

    UFUNCTION(BlueprintCallable, Category = "Terrain")
    void RebuildTerrain();

    UFUNCTION(BlueprintCallable, Category = "Terrain")
    void RemoveVoxel(FVector WorldLocation);

    UFUNCTION(BlueprintCallable, Category = "Terrain")
    void PlaceVoxel(FVector WorldLocation, EVoxelType Type = EVoxelType::Dirt);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Collision")
    TEnumAsByte<ECollisionEnabled::Type> CollisionEnabled = ECollisionEnabled::QueryAndPhysics;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Collision")
    FName CollisionProfileName = "BlockAll";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Collision")
    bool bGenerateOverlapEvents = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Rendering")
    bool bCastShadow = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Rendering")
    bool bReceivesDecals = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Collision")
    bool bUseComplexAsSimpleCollision = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Collision")
    bool bEnableComplexCollision = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Rendering")
    float TextureScale = 0.1f;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
    struct FVoxelChunk
    {
        FIntVector Coord;
        TArray<EVoxelType> VoxelData;
        TArray<TArray<int32>> VoxelTriangles; // triangle IDs per voxel
        UDynamicMeshComponent* MeshComponent = nullptr;

        void BuildMesh(ASmoothVoxelTerrain* TerrainOwner);
        void UpdateVoxel(int32 LocalX, int32 LocalY, int32 LocalZ, EVoxelType NewType, ASmoothVoxelTerrain* TerrainOwner);

        // Incremental editing methods
        void UpdateVoxelMesh(int32 LocalX, int32 LocalY, int32 LocalZ, EVoxelType NewType, ASmoothVoxelTerrain* TerrainOwner);
        void RemoveVoxelFaces(int32 LocalX, int32 LocalY, int32 LocalZ, FDynamicMesh3& Mesh, ASmoothVoxelTerrain* TerrainOwner);
        void AddVoxelFaces(int32 LocalX, int32 LocalY, int32 LocalZ, FDynamicMesh3& Mesh, ASmoothVoxelTerrain* TerrainOwner);
        void UpdateSharedFace(int32 LocalX, int32 LocalY, int32 LocalZ, ASmoothVoxelTerrain* TerrainOwner, const FIntVector& NeighborDirection);
    };

    TMap<FIntVector, TUniquePtr<FVoxelChunk>> Chunks;

    UPROPERTY(VisibleAnywhere)
    USceneComponent* RootSceneComponent = nullptr;

    void GenerateChunks();
    FIntVector WorldToChunkCoord(const FVector& WorldPos) const;
    void WorldToLocalVoxel(const FVector& WorldPos, const FIntVector& ChunkCoord, int32& OutX, int32& OutY, int32& OutZ) const;
    FVector ChunkCoordToWorldOrigin(const FIntVector& ChunkCoord) const;

    EVoxelType GetVoxelAtWorld(int32 WorldX, int32 WorldY, int32 WorldZ) const;
    float GetHeightAtWorldCorner(int32 WorldX, int32 WorldY) const;
    float GetInterpolatedHeight(float WorldX, float WorldY) const;

    FVector GetSmoothVertexWorld(int32 WorldX, int32 WorldY, int32 WorldZ, int32 VoxX, int32 VoxY, int32 VoxZ) const;
    FVector GetSmoothNormalWorld(int32 WorldX, int32 WorldY) const;
    float GetNeighborTopHeightWorld(int32 WorldX, int32 WorldY, int32 WorldZ, const FVector& Vertex) const;

    void AppendVoxelFacesWorld(int32 WorldX, int32 WorldY, int32 WorldZ, UE::Geometry::FDynamicMesh3& Mesh, TArray<int32>& OutTriIDs);

    //void RebuildAllChunks();
    //void RebuildChunk(const FIntVector& ChunkCoord);
    FVoxelChunk* GetChunk(const FIntVector& Coord);
    const FVoxelChunk* GetChunk(const FIntVector& Coord) const;

    bool bCollisionDirty = false;
    void UpdateCollisionIfNeeded();

    bool bIsDestroyed = false;
};
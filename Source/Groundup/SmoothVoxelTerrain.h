#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/DynamicMeshComponent.h"
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

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    UMaterialInterface* TerrainMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    int32 ChunkSize = 32;               // X and Y size in voxels

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    int32 MaxHeight = 32;               // Z size in voxels

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    float CubeSize = 100.0f;            // World units per voxel

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    float NoiseScale = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    float HeightMultiplier = 2000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    float MinGrassThickness = 1.5f;     // In voxel units

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    int32 Seed = 1337;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    bool bSmoothTerrain = false;        // Set true for heightmap‑aligned tops

    UFUNCTION(BlueprintCallable, Category = "Terrain")
    void RebuildTerrain();

    UFUNCTION(BlueprintCallable, Category = "Terrain")
    void RemoveVoxel(FVector WorldLocation);

    UFUNCTION(BlueprintCallable, Category = "Terrain")
    void PlaceVoxel(FVector WorldLocation, EVoxelType Type = EVoxelType::Dirt);

private:
    UPROPERTY(EditAnywhere)
    UDynamicMeshComponent* MeshComponent = nullptr;

    TArray<EVoxelType> VoxelData;
    TArray<float> HeightMap;            // Size (ChunkSize+1)^2

    // Per‑voxel storage of triangle IDs in the current dynamic mesh.
    // Index = GetIndex(x,y,z). Each inner array holds IDs of all triangles belonging to that voxel.
    TArray<TArray<int32>> VoxelTriangles;

    bool bMeshBuilt = false;
    bool bIsDestroyed = false;

    // Helper functions
    void PrecomputeHeightMap();
    void GenerateVoxelData();
    void CreateMesh();                     // Full build (initial or after full clear)

    // Incremental update: rebuild only the voxel at (x,y,z) and its 6 face‑neighbors.
    void UpdateVoxelRegion(int32 x, int32 y, int32 z);

    // Directly append faces of a single voxel into a dynamic mesh, recording triangle IDs.
    void AppendVoxelFaces(int32 x, int32 y, int32 z, UE::Geometry::FDynamicMesh3& Mesh, TArray<int32>& OutTriIDs);

    // Conversions
    int32 GetIndex(int32 x, int32 y, int32 z) const;
    void IndexToXYZ(int32 Idx, int32& OutX, int32& OutY, int32& OutZ) const;

    // Voxel data access
    EVoxelType GetVoxelAt(int32 x, int32 y, int32 z) const;
    float GetHeightAtCorner(int32 x, int32 y) const;
    float GetInterpolatedHeight(float WorldX, float WorldY) const;
    float GetNeighborTopHeight(int32 nx, int32 ny, int32 nz, const FVector& Vertex) const;

    // Vertex and normal generation
    FVector GetSmoothVertex(int32 CornerX, int32 CornerY, int32 CornerZ, int32 VoxX, int32 VoxY, int32 VoxZ) const;
    FVector GetSmoothNormal(int32 x, int32 y) const;

    // Legacy helper (kept for compatibility, not used in incremental path)
    void CreateFace(FVector p1, FVector p2, FVector p3, FVector p4, int32& VertexIdx,
        TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Norms, TArray<FVector2D>& UVs);
};
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/DynamicMeshComponent.h"
#include "SmoothVoxelTerrain.generated.h"

UENUM(BlueprintType)
enum class EVoxelType : uint8
{
	Air,
	Grass,
	Dirt,
	Stone
};

UCLASS()
class GROUNDUP_API ASmoothVoxelTerrain : public AActor
{
	GENERATED_BODY()

public:
	ASmoothVoxelTerrain();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Settings")
	int32 Seed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Settings")
	float CubeSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Settings")
	int32 ChunkSize = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Settings")
	int32 MaxHeight = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Noise")
	float NoiseScale = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Noise")
	float HeightMultiplier = 20.0f;

	UFUNCTION(BlueprintCallable, Category = "Voxel")
	void RemoveVoxel(FVector WorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Voxel")
	void PlaceVoxel(FVector WorldLocation, EVoxelType Type = EVoxelType::Dirt);

	UFUNCTION(CallInEditor, Category = "Voxel")
	void RebuildTerrain();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Terrain")
	bool bSmoothTerrain = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	float MinGrassThickness = 0.f;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual ~ASmoothVoxelTerrain() override;


private:
	UPROPERTY(VisibleAnywhere)
	UDynamicMeshComponent* Mesh;

	UPROPERTY()
	TArray<EVoxelType> VoxelData;

	TMap<int32, TArray<int32>> VoxelTriangleMap;

	UPROPERTY()
	TArray<float> HeightMap;

	void GenerateVoxelData();
	void PrecomputeHeightMap();
	void CreateMesh();

	// Logic Helpers
	int32 GetIndex(int32 x, int32 y, int32 z) const;
	EVoxelType GetVoxelAt(int32 x, int32 y, int32 z) const;
	float GetHeightAtCorner(int32 x, int32 y) const;
	FVector GetSmoothVertex(int32 cornerX, int32 cornerY, int32 cornerZ, int32 vX, int32 vY, int32 vZ) const;
	FVector GetSmoothNormal(int32 x, int32 y) const;
	float GetInterpolatedHeight(float X, float Y) const;

	void GenerateVoxelMesh(int32 x, int32 y, int32 z,
		TArray<FVector>& OutVertices, TArray<int32>& OutTriangles) const;

	void RegenerateVoxelAndNeighbors(int32 CenterX, int32 CenterY, int32 CenterZ);



	void CreateFace(FVector p1, FVector p2, FVector p3, FVector p4, int32& VertexIdx,
		TArray<FVector>& Verts, TArray<int32>& Tris) const;

	float GetNeighborTopHeight(int32 neighborX, int32 neighborY, int32 neighborZ, const FVector& point) const;

	bool bIsDestroyed = false;
};
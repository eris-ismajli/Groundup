#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
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

	UFUNCTION(CallInEditor, Category = "Voxel")
	void RebuildTerrain();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Terrain")
	bool bSmoothTerrain = true;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(VisibleAnywhere)
	UProceduralMeshComponent* Mesh;

	UPROPERTY()
	TArray<EVoxelType> VoxelData;

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

	void CreateFace(FVector p1, FVector p2, FVector p3, FVector p4, int32& VertexIndex,
		TArray<FVector>& Verts, TArray<int32>& Tris, TArray<FVector>& Norms, TArray<FVector2D>& UVs, TArray<FLinearColor>& Colors);

#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);
#endif
};
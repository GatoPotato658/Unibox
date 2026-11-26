#include "D3DMaterials.h"

#if defined(_WIN32) && !defined(TEXTMODE)
#include "RenderQueue.h"
#include "D3DMaterialShaders.h"
#include <d3d9.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

struct D3DGeometryData
{
	struct Strip
	{
		int start = 0;
		int count = 0;
		bool triangleStrip = false;
		std::vector<std::pair<int, int>> changes;
	};

	struct EngineMesh
	{
		std::byte padding[0x18];
		VertexFormat_t format;
		std::byte padding20[8];
		struct LegacyVertexBuffer
		{
			IDirect3DVertexBuffer9* buffer;
			std::uint64_t format;
			int bytes;
			int position;
			int count;
			int stride;
		}* vertices;
		struct LegacyIndexBuffer
		{
			IDirect3DIndexBuffer9* buffer;
		}* indices;
	};

	struct Group
	{
		EngineMesh* mesh = nullptr;
		VertexFormat_t format = 0;
		int meshId = -1;
		int groupIndex = -1;
		int vertices = 0;
		int declaration = 0;
		int minimumStride = 0;
		int maxHardwareID = -1;
		bool skinned = false;
		std::vector<Strip> strips;
	};

	struct UsedBone
	{
		int boneIndex = 0;
		matrix3x4 poseToBone = {};
	};

	const studiohdr_t* header = nullptr;
	const studiohwdata_t* hardware = nullptr;
	const void* meshDataPtr = nullptr;
	int lod = 0;
	int body = 0;
	int numBones = 0;
	int numBodyParts = 0;
	int numMeshes = 0;
	bool staticProp = false;
	bool needsFallback = false;
	std::vector<Group> groups;
	std::vector<UsedBone> usedBones;
};

struct D3DPoseMatrix
{
	float m[3][4] = {};
};

struct D3DPoseData
{
	std::vector<D3DPoseMatrix> poses;
};

namespace
{
	using Microsoft::WRL::ComPtr;
	using EngineMesh = D3DGeometryData::EngineMesh;
	using LegacyVertexBuffer = EngineMesh::LegacyVertexBuffer;
	using LegacyIndexBuffer = EngineMesh::LegacyIndexBuffer;

	constexpr int HardwareBones = 53;
	constexpr int PaletteRegister = 8;
	constexpr int EffectCount = 39;
	constexpr std::uint64_t InvalidGeneration = ~std::uint64_t(0);
	constexpr std::size_t MaxPosePool = 96;
	constexpr std::size_t MaxBatchPool = 8;
	constexpr std::size_t MaxGeometryCache = 512;

	constexpr D3DRENDERSTATETYPE MaterialStates[] =
	{
		D3DRS_ZENABLE, D3DRS_ZFUNC, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE,
		D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_BLENDOP, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_FILLMODE,
		D3DRS_FOGENABLE, D3DRS_LIGHTING, D3DRS_SRGBWRITEENABLE, D3DRS_COLORWRITEENABLE, D3DRS_DEPTHBIAS,
		D3DRS_SLOPESCALEDEPTHBIAS, D3DRS_CULLMODE, D3DRS_STENCILENABLE, D3DRS_STENCILFAIL,
		D3DRS_STENCILZFAIL, D3DRS_STENCILPASS, D3DRS_STENCILFUNC, D3DRS_STENCILREF,
		D3DRS_STENCILMASK, D3DRS_STENCILWRITEMASK
	};

	static_assert(sizeof(void*) == 8);
	static_assert(offsetof(LegacyVertexBuffer, buffer) == 0);
	static_assert(offsetof(LegacyVertexBuffer, count) == 0x18);
	static_assert(offsetof(LegacyVertexBuffer, stride) == 0x1c);
	static_assert(offsetof(EngineMesh, format) == 0x18);
	static_assert(offsetof(EngineMesh, vertices) == 0x28);
	static_assert(offsetof(EngineMesh, indices) == 0x30);
	static_assert(sizeof(CFunctor) == 16);

#pragma pack(push, 1)
	struct PackedBoneChange
	{
		int hardwareID;
		int newBoneID;
	};

	struct PackedStrip
	{
		int numIndices;
		int indexOffset;
		int numVerts;
		int vertOffset;
		short numBones;
		unsigned char flags;
		int numBoneStateChanges;
		int boneStateChangeOffset;
	};
#pragma pack(pop)

	static_assert(sizeof(PackedStrip) == 27);
	static_assert(offsetof(PackedStrip, numBoneStateChanges) == 19);
	static_assert(sizeof(PackedBoneChange) == 8);

	struct CameraData
	{
		float viewProjection[4][4] = {};
		float eye[4] = {};
	};

	struct DrawItem
	{
		CD3DMaterials::GeometryRef geometry;
		CD3DMaterials::PoseRef poses;
		ChamsMaterial_t params;
		D3DStencilState_t stencil;
		std::uint16_t cameraIndex = 0;
		bool occluded = false;
		bool maskOnly = false;
		bool useExplicitStencil = false;
		bool useExplicitCull = false;
		bool invertCull = false;
		float time = 0.f;
	};

	struct Batch
	{
		std::uint64_t generation = 0;
		std::vector<CameraData> cameras;
		std::vector<DrawItem> items;
		bool anyOccluded = false;

		void Clear()
		{
			generation = 0;
			cameras.clear();
			items.clear();
			anyOccluded = false;
		}
	};

	struct GeometryKey
	{
		const studiohdr_t* header = nullptr;
		const studiohwdata_t* hardware = nullptr;
		int lod = 0;
		int body = 0;

		bool operator==(const GeometryKey&) const = default;
	};

	struct GeometryKeyHash
	{
		std::size_t operator()(const GeometryKey& key) const noexcept
		{
			std::size_t h = reinterpret_cast<std::uintptr_t>(key.header);
			h ^= reinterpret_cast<std::uintptr_t>(key.hardware) * 0x9e3779b97f4a7c15ull + 0x9e3779b9 + (h << 6) + (h >> 2);
			h ^= std::size_t(key.lod) * 0x517cc1b727220a95ull + (h << 6) + (h >> 2);
			h ^= std::size_t(key.body) + 0x9e3779b9 + (h << 6) + (h >> 2);
			return h;
		}
	};

	struct Resources
	{
		ComPtr<IDirect3DDevice9> device;
		ComPtr<IDirect3DStateBlock9> state;
		ComPtr<IDirect3DVertexShader9> rigid;
		ComPtr<IDirect3DVertexShader9> skinned;
		std::array<ComPtr<IDirect3DVertexDeclaration9>, 8> declarations;
		std::array<ComPtr<IDirect3DPixelShader9>, EffectCount> pixels;
	};

	Resources s_Resources;
	std::mutex s_Mutex;
	std::mutex s_CacheMutex;
	std::mutex s_PoolMutex;
	std::atomic<std::uint64_t> s_Generation = 0;
	std::atomic<std::uint64_t> s_ReadyGeneration = InvalidGeneration;
	std::atomic<std::uint64_t> s_InitializationGeneration = InvalidGeneration;
	std::atomic<std::uint64_t> s_WarmupGeneration = InvalidGeneration;

	std::unordered_map<GeometryKey, CD3DMaterials::GeometryRef, GeometryKeyHash> s_GeometryCache;
	std::vector<D3DPoseData*> s_PosePool;
	std::vector<Batch*> s_BatchPool;

	struct BatchBuilder
	{
		int depth = 0;
		std::shared_ptr<Batch> batch;
		IMatRenderContext* context = nullptr;
	} s_ActiveBatch;

	std::shared_ptr<D3DPoseData> AcquirePoseData()
	{
		D3DPoseData* raw = nullptr;
		{
			std::scoped_lock lock(s_PoolMutex);
			if (!s_PosePool.empty())
			{
				raw = s_PosePool.back();
				s_PosePool.pop_back();
			}
		}
		if (!raw)
			raw = new (std::nothrow) D3DPoseData();
		if (!raw)
			return {};
		raw->poses.clear();
		return std::shared_ptr<D3DPoseData>(raw, [](D3DPoseData* ptr)
		{
			if (!ptr)
				return;
			ptr->poses.clear();
			std::scoped_lock lock(s_PoolMutex);
			if (s_PosePool.size() < MaxPosePool)
				s_PosePool.push_back(ptr);
			else
				delete ptr;
		});
	}

	std::shared_ptr<Batch> AcquireBatch(std::uint64_t generation)
	{
		Batch* raw = nullptr;
		{
			std::scoped_lock lock(s_PoolMutex);
			if (!s_BatchPool.empty())
			{
				raw = s_BatchPool.back();
				s_BatchPool.pop_back();
			}
		}
		if (!raw)
			raw = new (std::nothrow) Batch();
		if (!raw)
			return {};
		raw->Clear();
		raw->generation = generation;
		return std::shared_ptr<Batch>(raw, [](Batch* ptr)
		{
			if (!ptr)
				return;
			ptr->Clear();
			std::scoped_lock lock(s_PoolMutex);
			if (s_BatchPool.size() < MaxBatchPool)
				s_BatchPool.push_back(ptr);
			else
				delete ptr;
		});
	}

	void DrainPools()
	{
		std::scoped_lock lock(s_PoolMutex);
		for (auto* ptr : s_PosePool)
			delete ptr;
		s_PosePool.clear();
		for (auto* ptr : s_BatchPool)
			delete ptr;
		s_BatchPool.clear();
	}

	bool FiniteMatrix(const matrix3x4& matrix)
	{
		for (const auto& row : matrix)
		{
			for (float value : row)
			{
				if (!std::isfinite(value))
					return false;
			}
		}
		return true;
	}

	void Identity(matrix3x4& matrix)
	{
		std::memset(matrix, 0, sizeof(matrix));
		matrix[0][0] = 1.f;
		matrix[1][1] = 1.f;
		matrix[2][2] = 1.f;
	}

	void Concat(const matrix3x4& left, const matrix3x4& right, matrix3x4& output)
	{
		for (int row = 0; row < 3; ++row)
		{
			output[row][0] = left[row][0] * right[0][0] + left[row][1] * right[1][0] + left[row][2] * right[2][0];
			output[row][1] = left[row][0] * right[0][1] + left[row][1] * right[1][1] + left[row][2] * right[2][1];
			output[row][2] = left[row][0] * right[0][2] + left[row][1] * right[1][2] + left[row][2] * right[2][2];
			output[row][3] = left[row][0] * right[0][3] + left[row][1] * right[1][3] + left[row][2] * right[2][3] + left[row][3];
		}
	}

	int MakeDeclaration(int index, D3DVERTEXELEMENT9(&elements)[5])
	{
		const bool compressed = (index & 1) != 0;
		int count = 0;
		WORD offset = 12;
		elements[count++] = { 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 };
		if (index & 2)
		{
			elements[count++] = { 0, offset, BYTE(compressed ? D3DDECLTYPE_SHORT2 : D3DDECLTYPE_FLOAT2), D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 0 };
			offset += compressed ? 4 : 8;
		}
		if (index & 4)
		{
			elements[count++] = { 0, offset, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0 };
			offset += 4;
		}
		elements[count++] = { 0, offset, BYTE(compressed ? D3DDECLTYPE_UBYTE4 : D3DDECLTYPE_FLOAT3), D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 };
		elements[count] = D3DDECL_END();
		return offset + (compressed ? 4 : 12);
	}

	bool CopyGroup(const studiomeshgroup_t& source, int boneCount, const std::array<signed char, MAXSTUDIOBONES>& boneValid, int (&boneToCompact)[MAXSTUDIOBONES], std::vector<int>& usedOrder, D3DGeometryData::Group& group)
	{
		if (!source.m_pMesh || source.m_MeshNeedsRestore ||
			(source.m_Flags & ~(MESHGROUP_IS_HWSKINNED | MESHGROUP_IS_FLEXED | MESHGROUP_IS_DELTA_FLEXED)) ||
			source.m_NumStrips <= 0 || source.m_NumStrips > 65536 || !source.m_pStripData ||
			source.m_NumVertices <= 0 || source.m_NumVertices > 65536)
			return false;

		group.mesh = reinterpret_cast<EngineMesh*>(source.m_pMesh);
		group.format = group.mesh->format;
		group.vertices = source.m_NumVertices;
		const bool weights = (group.format & VERTEX_BONE_WEIGHT_MASK) != 0;
		const bool indices = (group.format & VERTEX_BONE_INDEX) != 0;
		if ((group.format & (VERTEX_POSITION | VERTEX_NORMAL)) != (VERTEX_POSITION | VERTEX_NORMAL) ||
			(weights && (group.format & VERTEX_BONE_WEIGHT_MASK) != VERTEX_BONEWEIGHT(2)) ||
			(weights && !indices) || (group.format & VERTEX_WRINKLE))
			return false;
		group.skinned = boneCount > 1;
		if (group.skinned && (!(source.m_Flags & MESHGROUP_IS_HWSKINNED) || !weights || !indices))
			return false;
		group.declaration = ((group.format & VERTEX_FORMAT_COMPRESSED) ? 1 : 0) | (weights ? 2 : 0) | (indices ? 4 : 0);
		D3DVERTEXELEMENT9 elements[5];
		group.minimumStride = MakeDeclaration(group.declaration, elements);
		group.strips.reserve(source.m_NumStrips);
		group.maxHardwareID = -1;

		std::array<int, MAXSTUDIOBONES> localBoneMap;
		localBoneMap.fill(-1);
		std::vector<int> localNewBones;

		const auto strips = reinterpret_cast<const PackedStrip*>(source.m_pStripData);
		bool hasPalette = !group.skinned;
		for (int i = 0; i < source.m_NumStrips; ++i)
		{
			const auto& input = strips[i];
			if (input.indexOffset < 0 || input.numIndices < 3 ||
				input.numVerts <= 0 || input.vertOffset < 0 ||
				input.numVerts > source.m_NumVertices || input.vertOffset > source.m_NumVertices - input.numVerts ||
				input.numBones < 0 || input.numBones > HardwareBones ||
				input.numBoneStateChanges < 0 || input.numBoneStateChanges > HardwareBones ||
				(input.numBoneStateChanges && input.boneStateChangeOffset < int(sizeof(PackedStrip))) ||
				(input.flags != 1 && input.flags != 2))
				return false;
			D3DGeometryData::Strip strip;
			strip.start = input.indexOffset;
			strip.count = input.numIndices;
			strip.triangleStrip = (input.flags & 2) != 0;
			if (!strip.triangleStrip && strip.count % 3)
				return false;
			if (group.skinned)
			{
				const auto changes = reinterpret_cast<const PackedBoneChange*>(reinterpret_cast<const std::byte*>(&input) + input.boneStateChangeOffset);
				strip.changes.reserve(input.numBoneStateChanges);
				for (int change = 0; change < input.numBoneStateChanges; ++change)
				{
					const auto& item = changes[change];
					if (item.newBoneID < 0)
						break;
					if (item.hardwareID < 0 || item.hardwareID >= HardwareBones || item.newBoneID >= boneCount)
						return false;
					if (boneValid[item.newBoneID] <= 0)
						return false;
					int compactID = boneToCompact[item.newBoneID];
					if (compactID < 0)
					{
						if (localBoneMap[item.newBoneID] < 0)
						{
							localBoneMap[item.newBoneID] = int(usedOrder.size() + localNewBones.size());
							localNewBones.push_back(item.newBoneID);
						}
						compactID = localBoneMap[item.newBoneID];
					}
					strip.changes.emplace_back(item.hardwareID, compactID);
					if (item.hardwareID > group.maxHardwareID)
						group.maxHardwareID = item.hardwareID;
					hasPalette = true;
				}
				if (!hasPalette || !input.numBones)
					return false;
			}
			group.strips.push_back(std::move(strip));
		}
		if (!group.skinned)
		{
			if (boneValid[0] <= 0)
				return false;
			if (boneToCompact[0] < 0)
			{
				boneToCompact[0] = int(usedOrder.size());
				usedOrder.push_back(0);
			}
		}
		else
		{
			for (int boneID : localNewBones)
			{
				boneToCompact[boneID] = int(usedOrder.size());
				usedOrder.push_back(boneID);
			}
		}
		return true;
	}

	std::shared_ptr<D3DGeometryData> BuildGeometry(const DrawModelState_t& state, const ModelRenderInfo_t& info, bool& anyRestoreFlag)
	{
		anyRestoreFlag = false;
		const auto header = state.m_pStudioHdr;
		const auto hardware = state.m_pStudioHWData;
		if (!header || !hardware || header->numbones <= 0 || header->numbones > MAXSTUDIOBONES ||
			header->numbodyparts <= 0 || header->numbodyparts > 256 || info.body < 0 ||
			state.m_lod < 0 || state.m_lod >= hardware->m_NumLODs || state.m_lod >= MAX_NUM_LODS ||
			!hardware->m_pLODs || hardware->m_NumStudioMeshes <= 0)
			return nullptr;
		const auto& lod = hardware->m_pLODs[state.m_lod];
		if (!lod.m_pMeshData)
			return nullptr;
		const bool staticProp = (header->flags & STUDIOHDR_FLAGS_STATIC_PROP) != 0;
		const auto linear = header->pLinearBones();
		if ((staticProp && header->numbones != 1) || (linear && linear->numbones != header->numbones))
			return nullptr;

		auto geom = std::make_shared<D3DGeometryData>();
		geom->header = header;
		geom->hardware = hardware;
		geom->meshDataPtr = lod.m_pMeshData;
		geom->lod = state.m_lod;
		geom->body = info.body;
		geom->numBones = header->numbones;
		geom->numBodyParts = header->numbodyparts;
		geom->numMeshes = hardware->m_NumStudioMeshes;
		geom->staticProp = staticProp;

		std::array<signed char, MAXSTUDIOBONES> boneValid = {};
		std::array<matrix3x4, MAXSTUDIOBONES> poseToBoneCache = {};
		for (int bone = 0; bone < header->numbones; ++bone)
		{
			const int flags = linear ? linear->flags(bone) : header->pBone(bone)->flags;
			if ((flags & (BONE_SCREEN_ALIGN_SPHERE | BONE_SCREEN_ALIGN_CYLINDER)) ||
				(!staticProp && !(flags & BONE_USED_BY_VERTEX_AT_LOD(state.m_lod))))
			{
				boneValid[bone] = -1;
				continue;
			}
			if (!staticProp)
			{
				const auto& ptb = linear ? *linear->poseToBone(bone) : header->pBone(bone)->poseToBone;
				if (!FiniteMatrix(ptb))
				{
					boneValid[bone] = -1;
					continue;
				}
				std::memcpy(poseToBoneCache[bone], ptb, sizeof(matrix3x4));
			}
			boneValid[bone] = 1;
		}

		int boneToCompact[MAXSTUDIOBONES];
		std::fill(std::begin(boneToCompact), std::end(boneToCompact), -1);
		std::vector<int> usedOrder;
		usedOrder.reserve(size_t(header->numbones));

		for (int part = 0; part < header->numbodyparts; ++part)
		{
			const auto body = header->pBodypart(part);
			if (body->nummodels <= 0 || body->nummodels > 256 || body->base <= 0)
				return nullptr;
			const auto model = body->pModel(info.body / body->base % body->nummodels);
			if (model->nummeshes < 0 || model->nummeshes > hardware->m_NumStudioMeshes)
				return nullptr;
			for (int mesh = 0; mesh < model->nummeshes; ++mesh)
			{
				const auto studioMesh = model->pMesh(mesh);
				if (studioMesh->meshid < 0 || studioMesh->meshid >= hardware->m_NumStudioMeshes)
					return nullptr;
				const auto& meshData = lod.m_pMeshData[studioMesh->meshid];
				if (meshData.m_NumGroup < 0 || meshData.m_NumGroup > 65536 || (meshData.m_NumGroup && !meshData.m_pMeshGroup))
					return nullptr;
				for (int groupIndex = 0; groupIndex < meshData.m_NumGroup; ++groupIndex)
				{
					const auto& source = meshData.m_pMeshGroup[groupIndex];
					if (source.m_MeshNeedsRestore)
						anyRestoreFlag = true;
					D3DGeometryData::Group group;
					if (!CopyGroup(source, header->numbones, boneValid, boneToCompact, usedOrder, group))
					{
						geom->needsFallback = true;
						continue;
					}
					group.meshId = studioMesh->meshid;
					group.groupIndex = groupIndex;
					geom->groups.push_back(std::move(group));
				}
			}
		}

		if (geom->groups.empty())
			return nullptr;

		geom->usedBones.resize(usedOrder.size());
		for (size_t i = 0; i < usedOrder.size(); ++i)
		{
			const int boneID = usedOrder[i];
			geom->usedBones[i].boneIndex = boneID;
			if (!staticProp)
				std::memcpy(geom->usedBones[i].poseToBone, poseToBoneCache[boneID], sizeof(matrix3x4));
		}
		return geom;
	}

	bool CaptureCamera(IMatRenderContext* context, CameraData& out)
	{
		VMatrix view, projection;
		context->GetMatrix(MATERIAL_VIEW, &view);
		context->GetMatrix(MATERIAL_PROJECTION, &projection);
		for (int row = 0; row < 4; ++row)
		{
			for (int column = 0; column < 4; ++column)
			{
				out.viewProjection[row][column] = projection[row][0] * view[0][column] + projection[row][1] * view[1][column] +
					projection[row][2] * view[2][column] + projection[row][3] * view[3][column];
				if (!std::isfinite(out.viewProjection[row][column]))
					return false;
			}
		}
		for (int axis = 0; axis < 3; ++axis)
		{
			out.eye[axis] = -(view[0][axis] * view[0][3] + view[1][axis] * view[1][3] + view[2][axis] * view[2][3]);
			if (!std::isfinite(out.eye[axis]))
				return false;
		}
		out.eye[3] = 1.f;
		return true;
	}

	bool CreateMaterialStateBlock(IDirect3DDevice9* device, IDirect3DStateBlock9** state)
	{
		if (FAILED(device->BeginStateBlock()))
			return false;

		const float constants[PaletteRegister + HardwareBones * 3][4] = {};
		bool success = true;
		for (auto type : MaterialStates)
		{
			DWORD value = 0;
			success = SUCCEEDED(device->GetRenderState(type, &value)) && success;
			success = SUCCEEDED(device->SetRenderState(type, value)) && success;
		}
		success = SUCCEEDED(device->SetVertexShader(nullptr)) && success;
		success = SUCCEEDED(device->SetPixelShader(nullptr)) && success;
		success = SUCCEEDED(device->SetVertexDeclaration(nullptr)) && success;
		success = SUCCEEDED(device->SetStreamSource(0, nullptr, 0, 0)) && success;
		success = SUCCEEDED(device->SetStreamSourceFreq(0, 1)) && success;
		success = SUCCEEDED(device->SetIndices(nullptr)) && success;
		success = SUCCEEDED(device->SetVertexShaderConstantF(0, constants[0], PaletteRegister + HardwareBones * 3)) && success;
		success = SUCCEEDED(device->SetPixelShaderConstantF(0, constants[0], 4)) && success;
		return SUCCEEDED(device->EndStateBlock(state)) && success;
	}

	bool EnsureResources(IDirect3DDevice9* device)
	{
		if (s_Resources.device.Get() == device && s_Resources.state)
			return true;
		s_Resources = {};
		D3DCAPS9 caps = {};
		if (FAILED(device->GetDeviceCaps(&caps)) || caps.VertexShaderVersion < D3DVS_VERSION(3, 0) ||
			caps.PixelShaderVersion < D3DPS_VERSION(3, 0) || caps.MaxVertexShaderConst < PaletteRegister + HardwareBones * 3)
			return false;
		Resources resources;
		resources.device = device;
		if (FAILED(device->CreateVertexShader(reinterpret_cast<const DWORD*>(D3DMaterialShaders::Rigid), resources.rigid.GetAddressOf())) ||
			FAILED(device->CreateVertexShader(reinterpret_cast<const DWORD*>(D3DMaterialShaders::Skinned), resources.skinned.GetAddressOf())))
			return false;
		if (!CreateMaterialStateBlock(device, resources.state.GetAddressOf()) &&
			FAILED(device->CreateStateBlock(D3DSBT_ALL, resources.state.ReleaseAndGetAddressOf())))
			return false;
		for (int index = 0; index < 8; ++index)
		{
			D3DVERTEXELEMENT9 elements[5];
			MakeDeclaration(index, elements);
			if (FAILED(device->CreateVertexDeclaration(elements, resources.declarations[index].GetAddressOf())))
				return false;
		}
		const std::uint32_t* const shaders[EffectCount] =
		{
			D3DMaterialShaders::Flat, D3DMaterialShaders::Shaded, D3DMaterialShaders::Metallic,
			D3DMaterialShaders::Pearlescent, D3DMaterialShaders::Glass, D3DMaterialShaders::Holographic,
			D3DMaterialShaders::Iridescent, D3DMaterialShaders::Chrome, D3DMaterialShaders::Gradient,
			D3DMaterialShaders::Wireframe, D3DMaterialShaders::Crystal, D3DMaterialShaders::Emissive,
			D3DMaterialShaders::Plasma, D3DMaterialShaders::Nebula, D3DMaterialShaders::Carbon,
			D3DMaterialShaders::Brushed, D3DMaterialShaders::Hex, D3DMaterialShaders::Digital,
			D3DMaterialShaders::Lava, D3DMaterialShaders::Bubble, D3DMaterialShaders::Neon,
			D3DMaterialShaders::Network, D3DMaterialShaders::Soap, D3DMaterialShaders::Aurora,
			D3DMaterialShaders::Aquarium, D3DMaterialShaders::Daydream, D3DMaterialShaders::Fresnel,
			D3DMaterialShaders::Galaxy, D3DMaterialShaders::Matrix, D3DMaterialShaders::Fire,
			D3DMaterialShaders::Frost, D3DMaterialShaders::Toon, D3DMaterialShaders::Cells,
			D3DMaterialShaders::Circuit, D3DMaterialShaders::Electric, D3DMaterialShaders::Spectrum,
			D3DMaterialShaders::Marble, D3DMaterialShaders::Thermal, D3DMaterialShaders::Camo
		};
		for (int effect = 0; effect < EffectCount; ++effect)
		{
			if (FAILED(device->CreatePixelShader(reinterpret_cast<const DWORD*>(shaders[effect]), resources.pixels[effect].GetAddressOf())))
				return false;
		}
		s_Resources = std::move(resources);
		return true;
	}

	void InitializeResources(IDirect3DDevice9* device, std::uint64_t generation)
	{
		std::scoped_lock lock(s_Mutex);
		if (!device || generation != s_Generation.load(std::memory_order_acquire) ||
			s_InitializationGeneration.load(std::memory_order_acquire) == generation)
			return;
		s_InitializationGeneration.store(generation, std::memory_order_release);
		if (SUCCEEDED(device->TestCooperativeLevel()) && EnsureResources(device))
			s_ReadyGeneration.store(generation, std::memory_order_release);
	}

	struct ValidatedMesh
	{
		EngineMesh* mesh = nullptr;
		IDirect3DVertexBuffer9* vb = nullptr;
		IDirect3DIndexBuffer9* ib = nullptr;
		UINT stride = 0;
		UINT vertexCount = 0;
		bool valid = false;
	};

	const ValidatedMesh* ValidateMeshCached(EngineMesh* mesh, VertexFormat_t format, int minVertices, int minStride, const std::vector<D3DGeometryData::Strip>& strips, IDirect3DDevice9* expectedDevice, std::vector<ValidatedMesh>& cache)
	{
		if (!mesh)
			return nullptr;
		for (const auto& entry : cache)
		{
			if (entry.mesh == mesh)
				return entry.valid ? &entry : nullptr;
		}

		ValidatedMesh entry;
		entry.mesh = mesh;
		using HandleLateCreation = void(__fastcall*)(void*);
		auto table = *reinterpret_cast<void***>(mesh);
		reinterpret_cast<HandleLateCreation>(table[37])(mesh);
		const auto vertices = mesh->vertices;
		const auto indices = mesh->indices;
		D3DVERTEXBUFFER_DESC vertexDesc = {};
		D3DINDEXBUFFER_DESC indexDesc = {};
		if (mesh->format == format && vertices && indices && vertices->buffer && indices->buffer &&
			vertices->stride >= minStride && vertices->count >= minVertices &&
			SUCCEEDED(vertices->buffer->GetDesc(&vertexDesc)) && SUCCEEDED(indices->buffer->GetDesc(&indexDesc)) &&
			indexDesc.Format == D3DFMT_INDEX16 &&
			std::uint64_t(vertices->count) * std::uint64_t(vertices->stride) <= vertexDesc.Size)
		{
			const std::uint64_t maxIndices = indexDesc.Size / sizeof(unsigned short);
			bool stripsOk = true;
			for (const auto& strip : strips)
			{
				if (std::uint64_t(strip.start) + std::uint64_t(strip.count) > maxIndices)
				{
					stripsOk = false;
					break;
				}
			}
			if (stripsOk)
			{
				ComPtr<IDirect3DDevice9> meshDevice;
				if (SUCCEEDED(vertices->buffer->GetDevice(meshDevice.GetAddressOf())) && meshDevice.Get() == expectedDevice)
				{
					entry.vb = vertices->buffer;
					entry.ib = indices->buffer;
					entry.stride = UINT(vertices->stride);
					entry.vertexCount = UINT(vertices->count);
					entry.valid = true;
				}
			}
		}
		cache.push_back(entry);
		return cache.back().valid ? &cache.back() : nullptr;
	}

	bool RenderBatch(const Batch& batch)
	{
		std::scoped_lock lock(s_Mutex);
		if (batch.items.empty() ||
			batch.generation != s_Generation.load(std::memory_order_acquire) ||
			s_ReadyGeneration.load(std::memory_order_acquire) != batch.generation)
			return false;

		IDirect3DDevice9* device = s_Resources.device.Get();
		if (!device || FAILED(device->TestCooperativeLevel()))
			return false;

		D3DVIEWPORT9 viewport = {};
		if ((batch.anyOccluded && FAILED(device->GetViewport(&viewport))) || FAILED(s_Resources.state->Capture()))
			return false;

		struct RestoreState
		{
			IDirect3DDevice9* device;
			IDirect3DStateBlock9* state;
			D3DVIEWPORT9 viewport;
			bool restoreViewport;
			~RestoreState()
			{
				state->Apply();
				if (restoreViewport)
					device->SetViewport(&viewport);
			}
		} restore{ device, s_Resources.state.Get(), viewport, batch.anyOccluded };

		if (batch.anyOccluded)
		{
			viewport.MinZ = 0.f;
			viewport.MaxZ = 1.f;
			if (FAILED(device->SetViewport(&viewport)))
				return false;
		}

		struct StateShadow
		{
			IDirect3DDevice9* device;
			bool initialized = false;
			DWORD rs[256] = {};
			bool rsValid[256] = {};
			IDirect3DPixelShader9* ps = nullptr;
			IDirect3DVertexShader9* vs = nullptr;
			IDirect3DVertexDeclaration9* decl = nullptr;
			IDirect3DVertexBuffer9* vb = nullptr;
			UINT vbStride = 0;
			IDirect3DIndexBuffer9* ib = nullptr;
			bool streamFreqSet = false;
			int compressedState = -1;
			std::uint16_t cameraIndex = 0xFFFF;
			float psConst[4][4] = {};
			bool psConstValid = false;

			bool SetRS(D3DRENDERSTATETYPE type, DWORD value)
			{
				const unsigned idx = unsigned(type);
				if (idx < 256 && rsValid[idx] && rs[idx] == value)
					return true;
				if (FAILED(device->SetRenderState(type, value)))
					return false;
				if (idx < 256)
				{
					rs[idx] = value;
					rsValid[idx] = true;
				}
				return true;
			}
		} shadow{ device };

		if (!shadow.SetRS(D3DRS_ZENABLE, D3DZB_TRUE) ||
			!shadow.SetRS(D3DRS_ALPHATESTENABLE, FALSE) ||
			!shadow.SetRS(D3DRS_SEPARATEALPHABLENDENABLE, FALSE) ||
			!shadow.SetRS(D3DRS_BLENDOP, D3DBLENDOP_ADD) ||
			!shadow.SetRS(D3DRS_FOGENABLE, FALSE) ||
			!shadow.SetRS(D3DRS_LIGHTING, FALSE) ||
			!shadow.SetRS(D3DRS_SRGBWRITEENABLE, FALSE) ||
			!shadow.SetRS(D3DRS_SLOPESCALEDEPTHBIAS, 0))
			return false;

		std::vector<ValidatedMesh> validatedMeshes;
		validatedMeshes.reserve(32);

		bool anyRendered = false;
		for (const auto& item : batch.items)
		{
			if (!item.geometry || !item.poses || item.cameraIndex >= batch.cameras.size())
				continue;

			if (shadow.cameraIndex != item.cameraIndex)
			{
				const auto& cam = batch.cameras[item.cameraIndex];
				if (FAILED(device->SetVertexShaderConstantF(0, cam.viewProjection[0], 4)) ||
					FAILED(device->SetVertexShaderConstantF(5, cam.eye, 1)))
					return anyRendered;
				shadow.cameraIndex = item.cameraIndex;
			}

			const auto& params = item.params;
			const int effectIndex = item.maskOnly ? 1 : params.iEffect;
			IDirect3DPixelShader9* wantPS = s_Resources.pixels[effectIndex - 1].Get();
			if (shadow.ps != wantPS)
			{
				if (FAILED(device->SetPixelShader(wantPS)))
					return anyRendered;
				shadow.ps = wantPS;
			}

			const float constants[4][4] =
			{
				{ params.tColor.r / 255.f, params.tColor.g / 255.f, params.tColor.b / 255.f, params.tColor.a / 255.f },
				{ params.tSecondary.r / 255.f, params.tSecondary.g / 255.f, params.tSecondary.b / 255.f, params.tSecondary.a / 255.f },
				{ std::clamp(params.flRoughness, 0.02f, 1.f), std::clamp(params.flReflectivity, 0.f, 1.f),
					std::clamp(params.flIntensity, 0.f, 10.f), std::clamp(params.flRimPower, 0.1f, 16.f) },
				{ item.time * std::clamp(params.flSpeed, -10.f, 10.f), std::clamp(params.flScale, 0.01f, 100.f), float(params.iBlend), 0.f }
			};
			if (!shadow.psConstValid || std::memcmp(shadow.psConst, constants, sizeof(constants)) != 0)
			{
				if (FAILED(device->SetPixelShaderConstantF(0, constants[0], 4)))
					return anyRendered;
				std::memcpy(shadow.psConst, constants, sizeof(constants));
				shadow.psConstValid = true;
			}

			bool ok = true;
			ok &= shadow.SetRS(D3DRS_ZFUNC, item.occluded ? D3DCMP_GREATER : D3DCMP_LESSEQUAL);
			ok &= shadow.SetRS(D3DRS_ZWRITEENABLE, (!item.maskOnly && params.bDepthWrite && !item.occluded) ? TRUE : FALSE);
			ok &= shadow.SetRS(D3DRS_ALPHABLENDENABLE, item.maskOnly ? FALSE : TRUE);
			if (!item.maskOnly)
			{
				ok &= shadow.SetRS(D3DRS_SRCBLEND, params.iBlend == 2 ? D3DBLEND_ONE : D3DBLEND_SRCALPHA);
				ok &= shadow.SetRS(D3DRS_DESTBLEND, params.iBlend == 1 ? D3DBLEND_ONE : params.iBlend == 2 ? D3DBLEND_INVSRCCOLOR : D3DBLEND_INVSRCALPHA);
			}
			ok &= shadow.SetRS(D3DRS_FILLMODE, (!item.maskOnly && params.iEffect == 10) ? D3DFILL_WIREFRAME : D3DFILL_SOLID);
			ok &= shadow.SetRS(D3DRS_COLORWRITEENABLE, item.maskOnly ? 0 : (D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE));
			const float depthBias = (item.maskOnly || item.occluded) ? 0.f : -1e-5f;
			ok &= shadow.SetRS(D3DRS_DEPTHBIAS, std::bit_cast<DWORD>(depthBias));
			if (item.useExplicitCull)
				ok &= shadow.SetRS(D3DRS_CULLMODE, item.invertCull ? D3DCULL_CW : D3DCULL_CCW);
			if (item.useExplicitStencil)
			{
				ok &= shadow.SetRS(D3DRS_STENCILENABLE, item.stencil.m_bEnable ? TRUE : FALSE);
				if (item.stencil.m_bEnable)
				{
					ok &= shadow.SetRS(D3DRS_STENCILFUNC, item.stencil.m_uCompareFunc);
					ok &= shadow.SetRS(D3DRS_STENCILPASS, item.stencil.m_uPassOp);
					ok &= shadow.SetRS(D3DRS_STENCILFAIL, item.stencil.m_uFailOp);
					ok &= shadow.SetRS(D3DRS_STENCILZFAIL, item.stencil.m_uZFailOp);
					ok &= shadow.SetRS(D3DRS_STENCILREF, item.stencil.m_uRef);
					ok &= shadow.SetRS(D3DRS_STENCILWRITEMASK, item.stencil.m_uWriteMask);
					ok &= shadow.SetRS(D3DRS_STENCILMASK, item.stencil.m_uTestMask);
				}
			}
			if (!ok)
				return anyRendered;

			if (!shadow.streamFreqSet)
			{
				if (FAILED(device->SetStreamSourceFreq(0, 1)))
					return anyRendered;
				shadow.streamFreqSet = true;
			}

			const auto& poses = item.poses->poses;
			for (const auto& group : item.geometry->groups)
			{
				const ValidatedMesh* vMesh = ValidateMeshCached(group.mesh, group.format, group.vertices, group.minimumStride, group.strips, device, validatedMeshes);
				if (!vMesh)
					continue;

				matrix3x4 palette[HardwareBones];
				const int paletteLimit = group.skinned && group.maxHardwareID >= 0 ? (group.maxHardwareID + 1) : 1;
				if (group.skinned)
				{
					for (int i = 0; i < paletteLimit; ++i)
						Identity(palette[i]);
				}

				IDirect3DVertexDeclaration9* wantDecl = s_Resources.declarations[group.declaration].Get();
				if (shadow.decl != wantDecl)
				{
					if (FAILED(device->SetVertexDeclaration(wantDecl)))
						return anyRendered;
					shadow.decl = wantDecl;
				}
				IDirect3DVertexShader9* wantVS = group.skinned ? s_Resources.skinned.Get() : s_Resources.rigid.Get();
				if (shadow.vs != wantVS)
				{
					if (FAILED(device->SetVertexShader(wantVS)))
						return anyRendered;
					shadow.vs = wantVS;
				}
				const int compressed = (group.format & VERTEX_FORMAT_COMPRESSED) != 0 ? 1 : 0;
				if (shadow.compressedState != compressed)
				{
					const float decode[4] = { 0.f, compressed ? 1.f / 32768.f : 1.f, compressed ? 1.f / 32768.f : 0.f, compressed ? 1.f : 0.f };
					if (FAILED(device->SetVertexShaderConstantF(4, decode, 1)))
						return anyRendered;
					shadow.compressedState = compressed;
				}
				if (shadow.vb != vMesh->vb || shadow.vbStride != vMesh->stride)
				{
					if (FAILED(device->SetStreamSource(0, vMesh->vb, 0, vMesh->stride)))
						return anyRendered;
					shadow.vb = vMesh->vb;
					shadow.vbStride = vMesh->stride;
				}
				if (shadow.ib != vMesh->ib)
				{
					if (FAILED(device->SetIndices(vMesh->ib)))
						return anyRendered;
					shadow.ib = vMesh->ib;
				}

				if (!group.skinned)
				{
					if (poses.empty() || FAILED(device->SetVertexShaderConstantF(PaletteRegister, poses[0].m[0], 3)))
						return anyRendered;
				}

				bool firstStrip = true;
				for (const auto& strip : group.strips)
				{
					int first = firstStrip && group.skinned ? 0 : HardwareBones;
					int last = firstStrip && group.skinned ? (paletteLimit - 1) : -1;
					if (group.skinned)
					{
						for (const auto& change : strip.changes)
						{
							if (size_t(change.second) < poses.size())
								std::memcpy(palette[change.first], poses[size_t(change.second)].m, sizeof(matrix3x4));
							first = std::min(first, change.first);
							last = std::max(last, change.first);
						}
						if (first <= last && FAILED(device->SetVertexShaderConstantF(PaletteRegister + first * 3, palette[first][0], (last - first + 1) * 3)))
							return anyRendered;
					}
					firstStrip = false;
					if (FAILED(device->DrawIndexedPrimitive(strip.triangleStrip ? D3DPT_TRIANGLESTRIP : D3DPT_TRIANGLELIST,
						0, 0, vMesh->vertexCount, strip.start, strip.triangleStrip ? strip.count - 2 : strip.count / 3)))
						return anyRendered;
					anyRendered = true;
				}
			}
		}
		return anyRendered;
	}

	bool ReadyOrRequestWarmup(IMatRenderContext* context, std::uint64_t& outGeneration)
	{
		outGeneration = s_Generation.load(std::memory_order_acquire);
		if (s_ReadyGeneration.load(std::memory_order_acquire) == outGeneration)
			return true;

		if (I::DirectXDevice && s_InitializationGeneration.load(std::memory_order_acquire) != outGeneration)
		{
			auto expected = InvalidGeneration;
			if (s_WarmupGeneration.compare_exchange_strong(expected, outGeneration, std::memory_order_acq_rel))
			{
				if (!QueueRenderCall(context, [device = I::DirectXDevice, generation = outGeneration] { InitializeResources(device, generation); }))
				{
					expected = outGeneration;
					s_WarmupGeneration.compare_exchange_strong(expected, InvalidGeneration, std::memory_order_acq_rel);
				}
			}
		}
		return false;
	}

	bool EnqueueBatch(IMatRenderContext* context, std::shared_ptr<Batch> batch)
	{
		if (!context || !batch || batch->items.empty())
			return false;

		if (auto queue = context->GetCallQueue())
		{
			auto call = [batch = std::move(batch)] { RenderBatch(*batch); };
			auto functor = new (std::nothrow) RenderQueue::CRenderCall<decltype(call)>(std::move(call));
			if (!functor)
				return false;
			context->Flush(false);
			queue->QueueFunctor(functor);
			functor->Release();
			return true;
		}
		context->Flush(false);
		return RenderBatch(*batch);
	}
}

void CD3DMaterials::Initialize(IDirect3DDevice9* pDevice)
{
	if (I::MaterialSystem && I::MaterialSystem->GetRenderBackend() == RENDER_BACKEND_D3D9)
		InitializeResources(pDevice, s_Generation.load(std::memory_order_acquire));
}

bool CD3DMaterials::PrepareGeometry(const DrawModelState_t& state, const ModelRenderInfo_t& info, GeometryRef& outGeometry, bool& outNeedsFallback)
{
	outGeometry.reset();
	outNeedsFallback = false;

	const auto header = state.m_pStudioHdr;
	const auto hardware = state.m_pStudioHWData;
	if (!header || !hardware || state.m_lod < 0 || state.m_lod >= hardware->m_NumLODs || !hardware->m_pLODs)
		return false;
	const auto& lod = hardware->m_pLODs[state.m_lod];
	const void* meshDataPtr = lod.m_pMeshData;
	if (!meshDataPtr)
		return false;

	const GeometryKey key{ header, hardware, state.m_lod, info.body };
	{
		std::scoped_lock lock(s_CacheMutex);
		if (auto it = s_GeometryCache.find(key); it != s_GeometryCache.end())
		{
			const auto& cached = it->second;
			bool valid = cached && cached->numBones == header->numbones && cached->meshDataPtr == meshDataPtr &&
				cached->numBodyParts == header->numbodyparts && cached->numMeshes == hardware->m_NumStudioMeshes;
			if (valid)
			{
				for (const auto& group : cached->groups)
				{
					if (group.meshId < 0 || group.meshId >= hardware->m_NumStudioMeshes)
					{
						valid = false;
						break;
					}
					const auto& md = lod.m_pMeshData[group.meshId];
					if (group.groupIndex < 0 || group.groupIndex >= md.m_NumGroup || !md.m_pMeshGroup)
					{
						valid = false;
						break;
					}
					const auto& src = md.m_pMeshGroup[group.groupIndex];
					if (src.m_MeshNeedsRestore || reinterpret_cast<const void*>(src.m_pMesh) != reinterpret_cast<const void*>(group.mesh))
					{
						valid = false;
						break;
					}
				}
			}
			if (valid)
			{
				outGeometry = cached;
				outNeedsFallback = cached->needsFallback;
				return true;
			}
			s_GeometryCache.erase(it);
		}
	}

	bool anyRestoreFlag = false;
	auto built = BuildGeometry(state, info, anyRestoreFlag);
	if (!built)
		return false;

	outGeometry = built;
	outNeedsFallback = built->needsFallback;
	if (!anyRestoreFlag)
	{
		std::scoped_lock lock(s_CacheMutex);
		if (s_GeometryCache.size() >= MaxGeometryCache)
			s_GeometryCache.clear();
		s_GeometryCache.emplace(key, std::move(built));
	}
	return true;
}

bool CD3DMaterials::PreparePoses(const GeometryRef& geometry, const matrix3x4* bones, PoseRef& outPoses)
{
	outPoses.reset();
	if (!geometry || !bones || geometry->usedBones.empty())
		return false;

	auto poseData = AcquirePoseData();
	if (!poseData)
		return false;

	poseData->poses.resize(geometry->usedBones.size());
	if (geometry->staticProp)
	{
		for (size_t i = 0; i < geometry->usedBones.size(); ++i)
		{
			const int boneIndex = geometry->usedBones[i].boneIndex;
			if (!FiniteMatrix(bones[boneIndex]))
				return false;
			std::memcpy(poseData->poses[i].m, bones[boneIndex], sizeof(matrix3x4));
		}
	}
	else
	{
		for (size_t i = 0; i < geometry->usedBones.size(); ++i)
		{
			const auto& ub = geometry->usedBones[i];
			if (!FiniteMatrix(bones[ub.boneIndex]))
				return false;
			Concat(bones[ub.boneIndex], ub.poseToBone, poseData->poses[i].m);
			if (!FiniteMatrix(poseData->poses[i].m))
				return false;
		}
	}

	outPoses = std::move(poseData);
	return true;
}

void CD3DMaterials::BeginBatch(IMatRenderContext* pContext)
{
	++s_ActiveBatch.depth;
	if (!s_ActiveBatch.context)
		s_ActiveBatch.context = pContext;
}

void CD3DMaterials::FlushBatch(IMatRenderContext* pContext)
{
	auto batch = std::exchange(s_ActiveBatch.batch, nullptr);
	IMatRenderContext* ctx = pContext ? pContext : s_ActiveBatch.context;
	if (batch && !batch->items.empty())
		EnqueueBatch(ctx, std::move(batch));
}

void CD3DMaterials::EndBatch(IMatRenderContext* pContext)
{
	if (s_ActiveBatch.depth > 0)
		--s_ActiveBatch.depth;
	if (s_ActiveBatch.depth == 0)
	{
		FlushBatch(pContext);
		s_ActiveBatch.context = nullptr;
	}
}

bool CD3DMaterials::DrawPrepared(IMatRenderContext* pContext, const GeometryRef& geometry, const PoseRef& poses, const ChamsMaterial_t& params, bool bOccluded, int iCullOverride, const D3DStencilState_t* pStencil, bool bMaskOnly)
{
	if (!pContext || !geometry || !poses || !I::MaterialSystem)
		return false;
	if (!bMaskOnly && (params.iEffect <= 0 || params.iEffect > EffectCount || params.iBlend < 0 || params.iBlend > 2 ||
		!std::isfinite(params.flRoughness) || !std::isfinite(params.flReflectivity) ||
		!std::isfinite(params.flScale) || !std::isfinite(params.flSpeed) ||
		!std::isfinite(params.flIntensity) || !std::isfinite(params.flRimPower)))
		return false;
	if (I::MaterialSystem->GetRenderBackend() != RENDER_BACKEND_D3D9)
		return false;

	std::uint64_t generation = 0;
	if (!ReadyOrRequestWarmup(pContext, generation))
		return false;

	const float time = I::GlobalVars ? I::GlobalVars->curtime : 0.f;
	if (!std::isfinite(time))
		return false;

	CameraData cam;
	if (!CaptureCamera(pContext, cam))
		return false;

	auto batch = s_ActiveBatch.depth > 0 ? s_ActiveBatch.batch : nullptr;
	if (!batch || batch->generation != generation)
	{
		if (s_ActiveBatch.depth > 0)
			s_ActiveBatch.batch.reset();
		if (batch && !batch->items.empty())
			EnqueueBatch(pContext, std::move(batch));
		batch = AcquireBatch(generation);
		if (!batch)
			return false;
		if (s_ActiveBatch.depth > 0)
			s_ActiveBatch.batch = batch;
	}

	std::uint16_t camIdx = 0;
	if (!batch->cameras.empty() && std::memcmp(&batch->cameras.back(), &cam, sizeof(CameraData)) == 0)
		camIdx = std::uint16_t(batch->cameras.size() - 1);
	else
	{
		camIdx = std::uint16_t(batch->cameras.size());
		batch->cameras.push_back(cam);
	}

	DrawItem item;
	item.geometry = geometry;
	item.poses = poses;
	item.params = params;
	if (pStencil)
	{
		item.stencil = *pStencil;
		item.useExplicitStencil = true;
	}
	item.useExplicitCull = (iCullOverride >= 0);
	item.invertCull = (iCullOverride == 1);
	item.cameraIndex = camIdx;
	item.occluded = bOccluded;
	item.maskOnly = bMaskOnly;
	item.time = time;
	batch->anyOccluded |= bOccluded;
	batch->items.push_back(std::move(item));

	if (s_ActiveBatch.depth == 0)
		return EnqueueBatch(pContext, std::move(batch));

	return true;
}

bool CD3DMaterials::Draw(const DrawModelState_t& state, const ModelRenderInfo_t& info, const matrix3x4* bones, const ChamsMaterial_t& params, bool bOccluded, bool bViewmodel)
{
	if (params.iEffect <= 0 || params.iEffect > EffectCount || params.iBlend < 0 || params.iBlend > 2 ||
		!std::isfinite(params.flRoughness) || !std::isfinite(params.flReflectivity) ||
		!std::isfinite(params.flScale) || !std::isfinite(params.flSpeed) ||
		!std::isfinite(params.flIntensity) || !std::isfinite(params.flRimPower) || !I::MaterialSystem)
		return false;
	if (I::MaterialSystem->GetRenderBackend() != RENDER_BACKEND_D3D9)
		return false;
	auto context = I::MaterialSystem->GetRenderContext();
	if (!context)
		return false;
	struct ReleaseContext
	{
		IMatRenderContext* context;
		~ReleaseContext() { context->Release(); }
	} release{ context };
	bool drewFallback = false;
	try
	{
		std::uint64_t generation = 0;
		if (!ReadyOrRequestWarmup(context, generation))
			return false;

		GeometryRef geom;
		bool needsFallback = false;
		if (!PrepareGeometry(state, info, geom, needsFallback))
			return false;
		PoseRef poses;
		if (!PreparePoses(geom, bones, poses))
			return false;

		DrawModelExecuteFn original = nullptr;
		if (needsFallback)
		{
			static auto hook = U::Hooks.Get("IVModelRender_DrawModelExecute");
			original = hook ? hook->As<DrawModelExecuteFn>() : nullptr;
			if (!original || !I::ModelRender)
				return false;
			FlushBatch(context);
			original(I::ModelRender, state, info, const_cast<matrix3x4*>(bones));
			drewFallback = true;
		}
		const bool rendered = DrawPrepared(context, geom, poses, params, bOccluded, -1, nullptr, false);
		return rendered || drewFallback;
	}
	catch (const std::bad_alloc&)
	{
		return drewFallback;
	}
}

void CD3DMaterials::ClearCache()
{
	std::scoped_lock lock(s_CacheMutex);
	s_GeometryCache.clear();
}

void CD3DMaterials::Unload()
{
	s_ActiveBatch.batch.reset();
	s_ActiveBatch.depth = 0;
	s_ActiveBatch.context = nullptr;
	{
		std::scoped_lock lock(s_Mutex);
		s_ReadyGeneration.store(InvalidGeneration, std::memory_order_release);
		s_Generation.fetch_add(1, std::memory_order_acq_rel);
		s_InitializationGeneration.store(InvalidGeneration, std::memory_order_release);
		s_WarmupGeneration.store(InvalidGeneration, std::memory_order_release);
		s_Resources = {};
	}
	ClearCache();
	DrainPools();
}
#else
void CD3DMaterials::Initialize(IDirect3DDevice9*)
{
}

bool CD3DMaterials::Draw(const DrawModelState_t&, const ModelRenderInfo_t&, const matrix3x4*, const ChamsMaterial_t&, bool, bool)
{
	return false;
}

bool CD3DMaterials::PrepareGeometry(const DrawModelState_t&, const ModelRenderInfo_t&, GeometryRef&, bool&)
{
	return false;
}

bool CD3DMaterials::PreparePoses(const GeometryRef&, const matrix3x4*, PoseRef&)
{
	return false;
}

bool CD3DMaterials::DrawPrepared(IMatRenderContext*, const GeometryRef&, const PoseRef&, const ChamsMaterial_t&, bool, int, const D3DStencilState_t*, bool)
{
	return false;
}

void CD3DMaterials::BeginBatch(IMatRenderContext*)
{
}

void CD3DMaterials::FlushBatch(IMatRenderContext*)
{
}

void CD3DMaterials::EndBatch(IMatRenderContext*)
{
}

void CD3DMaterials::ClearCache()
{
}

void CD3DMaterials::Unload()
{
}
#endif

#include "D3DMaterials.h"

#if defined(_WIN32) && !defined(TEXTMODE)
#include "RenderQueue.h"
#include "D3DMaterialShaders.h"
#include <d3d9.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace
{
	using Microsoft::WRL::ComPtr;

	constexpr int HardwareBones = 53;
	constexpr int PaletteRegister = 8;
	constexpr int EffectCount = 39;
	constexpr std::uint64_t InvalidGeneration = ~std::uint64_t(0);

	struct LegacyVertexBuffer
	{
		IDirect3DVertexBuffer9* buffer;
		std::uint64_t format;
		int bytes;
		int position;
		int count;
		int stride;
	};

	struct LegacyIndexBuffer
	{
		IDirect3DIndexBuffer9* buffer;
	};

	struct EngineMesh
	{
		std::byte padding[0x18];
		VertexFormat_t format;
		std::byte padding20[8];
		LegacyVertexBuffer* vertices;
		LegacyIndexBuffer* indices;
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

	struct Strip
	{
		int start = 0;
		int count = 0;
		bool triangleStrip = false;
		std::vector<PackedBoneChange> changes;
	};

	struct Group
	{
		EngineMesh* mesh = nullptr;
		VertexFormat_t format = 0;
		int vertices = 0;
		int declaration = 0;
		int minimumStride = 0;
		bool skinned = false;
		std::vector<Strip> strips;
	};

	struct Packet
	{
		ChamsMaterial_t params;
		std::vector<Group> groups;
		matrix3x4 poses[MAXSTUDIOBONES] = {};
		float viewProjection[4][4] = {};
		float eye[4] = {};
		float time = 0.f;
		bool occluded = false;
		bool needsFallback = false;
		std::uint64_t generation = 0;
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
	std::atomic<std::uint64_t> s_Generation = 0;
	std::atomic<std::uint64_t> s_ReadyGeneration = InvalidGeneration;
	std::atomic<std::uint64_t> s_InitializationGeneration = InvalidGeneration;
	std::atomic<std::uint64_t> s_WarmupGeneration = InvalidGeneration;

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
		for (int i = 0; i < 3; ++i)
			matrix[i][i] = 1.f;
	}

	void Concat(const matrix3x4& left, const matrix3x4& right, matrix3x4& output)
	{
		for (int row = 0; row < 3; ++row)
		{
			for (int column = 0; column < 4; ++column)
			{
				output[row][column] = left[row][0] * right[0][column] + left[row][1] * right[1][column] + left[row][2] * right[2][column];
				if (column == 3)
					output[row][column] += left[row][3];
			}
		}
	}

	int MakeDeclaration(int index, D3DVERTEXELEMENT9 (&elements)[5])
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

	bool CopyGroup(const studiomeshgroup_t& source, int boneCount, bool (&usedBones)[MAXSTUDIOBONES], Group& group)
	{
		if (!source.m_pMesh || source.m_MeshNeedsRestore || source.m_pMorph ||
			(source.m_Flags & (MESHGROUP_IS_FLEXED | MESHGROUP_IS_DELTA_FLEXED)) ||
			(source.m_Flags & ~MESHGROUP_IS_HWSKINNED) ||
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
			Strip strip;
			strip.start = input.indexOffset;
			strip.count = input.numIndices;
			strip.triangleStrip = (input.flags & 2) != 0;
			if (!strip.triangleStrip && strip.count % 3)
				return false;
			if (group.skinned)
			{
				const auto changes = reinterpret_cast<const PackedBoneChange*>(reinterpret_cast<const std::byte*>(&input) + input.boneStateChangeOffset);
				for (int change = 0; change < input.numBoneStateChanges; ++change)
				{
					const auto& item = changes[change];
					if (item.newBoneID < 0)
						break;
					if (item.hardwareID < 0 || item.hardwareID >= HardwareBones || item.newBoneID >= boneCount)
						return false;
					strip.changes.push_back(item);
					usedBones[item.newBoneID] = true;
					hasPalette = true;
				}
				if (!hasPalette || !input.numBones)
					return false;
			}
			group.strips.push_back(std::move(strip));
		}
		if (!group.skinned)
			usedBones[0] = true;
		return true;
	}

	bool CopyModel(const DrawModelState_t& state, const ModelRenderInfo_t& info, const matrix3x4* bones, Packet& packet)
	{
		const auto header = state.m_pStudioHdr;
		const auto hardware = state.m_pStudioHWData;
		if (!header || !hardware || !bones || header->numbones <= 0 || header->numbones > MAXSTUDIOBONES ||
			header->numbodyparts <= 0 || header->numbodyparts > 256 || info.body < 0 ||
			state.m_lod < 0 || state.m_lod >= hardware->m_NumLODs || state.m_lod >= MAX_NUM_LODS ||
			!hardware->m_pLODs || hardware->m_NumStudioMeshes <= 0)
			return false;
		const auto& lod = hardware->m_pLODs[state.m_lod];
		if (!lod.m_pMeshData)
			return false;
		const bool staticProp = (header->flags & STUDIOHDR_FLAGS_STATIC_PROP) != 0;
		const auto linear = header->pLinearBones();
		if ((staticProp && header->numbones != 1) || (linear && linear->numbones != header->numbones))
			return false;
		std::array<signed char, MAXSTUDIOBONES> boneStates = {};
		auto copyBone = [&](int bone)
		{
			if (boneStates[bone])
				return boneStates[bone] > 0;
			boneStates[bone] = -1;
			const int flags = linear ? linear->flags(bone) : header->pBone(bone)->flags;
			if ((flags & (BONE_SCREEN_ALIGN_SPHERE | BONE_SCREEN_ALIGN_CYLINDER)) ||
				(!staticProp && !(flags & BONE_USED_BY_VERTEX_AT_LOD(state.m_lod))) || !FiniteMatrix(bones[bone]))
				return false;
			if (staticProp)
				std::memcpy(packet.poses[bone], bones[bone], sizeof(matrix3x4));
			else
			{
				const auto& poseToBone = linear ? *linear->poseToBone(bone) : header->pBone(bone)->poseToBone;
				if (!FiniteMatrix(poseToBone))
					return false;
				Concat(bones[bone], poseToBone, packet.poses[bone]);
				if (!FiniteMatrix(packet.poses[bone]))
					return false;
			}
			boneStates[bone] = 1;
			return true;
		};
		for (int part = 0; part < header->numbodyparts; ++part)
		{
			const auto body = header->pBodypart(part);
			if (body->nummodels <= 0 || body->nummodels > 256 || body->base <= 0)
				return false;
			const auto model = body->pModel(info.body / body->base % body->nummodels);
			if (model->nummeshes < 0 || model->nummeshes > hardware->m_NumStudioMeshes)
				return false;
			for (int mesh = 0; mesh < model->nummeshes; ++mesh)
			{
				const auto studioMesh = model->pMesh(mesh);
				if (studioMesh->meshid < 0 || studioMesh->meshid >= hardware->m_NumStudioMeshes)
					return false;
				const auto& meshData = lod.m_pMeshData[studioMesh->meshid];
				if (meshData.m_NumGroup < 0 || meshData.m_NumGroup > 65536 || (meshData.m_NumGroup && !meshData.m_pMeshGroup))
					return false;
				for (int groupIndex = 0; groupIndex < meshData.m_NumGroup; ++groupIndex)
				{
					Group group;
					bool usedBones[MAXSTUDIOBONES] = {};
					if (!CopyGroup(meshData.m_pMeshGroup[groupIndex], header->numbones, usedBones, group))
					{
						packet.needsFallback = true;
						continue;
					}
					bool supported = true;
					for (int bone = 0; bone < header->numbones; ++bone)
					{
						if (usedBones[bone] && !copyBone(bone))
						{
							supported = false;
							break;
						}
					}
					if (!supported)
					{
						packet.needsFallback = true;
						continue;
					}
					packet.groups.push_back(std::move(group));
				}
			}
		}
		return !packet.groups.empty();
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
			FAILED(device->CreateVertexShader(reinterpret_cast<const DWORD*>(D3DMaterialShaders::Skinned), resources.skinned.GetAddressOf())) ||
			FAILED(device->CreateStateBlock(D3DSBT_ALL, resources.state.GetAddressOf())))
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

	bool Render(const Packet& packet)
	{
		std::scoped_lock lock(s_Mutex);
		if (packet.generation != s_Generation.load(std::memory_order_acquire) ||
			s_ReadyGeneration.load(std::memory_order_acquire) != packet.generation)
			return false;
		ComPtr<IDirect3DDevice9> device;
		for (const auto& group : packet.groups)
		{
			using HandleLateCreation = void(__fastcall*)(void*);
			auto table = *reinterpret_cast<void***>(group.mesh);
			reinterpret_cast<HandleLateCreation>(table[37])(group.mesh);
			const auto vertices = group.mesh->vertices;
			const auto indices = group.mesh->indices;
			if (group.mesh->format != group.format || !vertices || !indices || !vertices->buffer || !indices->buffer ||
				vertices->stride < group.minimumStride || vertices->count < group.vertices)
				return false;
			D3DVERTEXBUFFER_DESC vertexDesc = {};
			D3DINDEXBUFFER_DESC indexDesc = {};
			if (FAILED(vertices->buffer->GetDesc(&vertexDesc)) || FAILED(indices->buffer->GetDesc(&indexDesc)) ||
				indexDesc.Format != D3DFMT_INDEX16 ||
				std::uint64_t(vertices->count) * std::uint64_t(vertices->stride) > vertexDesc.Size)
				return false;
			for (const auto& strip : group.strips)
			{
				if (std::uint64_t(strip.start) + std::uint64_t(strip.count) > indexDesc.Size / sizeof(unsigned short))
					return false;
			}
			if (!device && FAILED(vertices->buffer->GetDevice(device.GetAddressOf())))
				return false;
		}
		D3DVIEWPORT9 viewport = {};
		if (!device || device.Get() != s_Resources.device.Get() || FAILED(device->TestCooperativeLevel()) ||
			FAILED(device->GetViewport(&viewport)) || FAILED(s_Resources.state->Capture()))
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
		} restore{ device.Get(), s_Resources.state.Get(), viewport, packet.occluded };
		if (packet.occluded)
		{
			viewport.MinZ = 0.f;
			viewport.MaxZ = 1.f;
			if (FAILED(device->SetViewport(&viewport)))
				return false;
		}
		const auto& params = packet.params;
		const float constants[4][4] =
		{
			{ params.tColor.r / 255.f, params.tColor.g / 255.f, params.tColor.b / 255.f, params.tColor.a / 255.f },
			{ params.tSecondary.r / 255.f, params.tSecondary.g / 255.f, params.tSecondary.b / 255.f, params.tSecondary.a / 255.f },
			{ std::clamp(params.flRoughness, 0.02f, 1.f), std::clamp(params.flReflectivity, 0.f, 1.f),
				std::clamp(params.flIntensity, 0.f, 10.f), std::clamp(params.flRimPower, 0.1f, 16.f) },
			{ packet.time * std::clamp(params.flSpeed, -10.f, 10.f), std::clamp(params.flScale, 0.01f, 100.f), float(params.iBlend), 0.f }
		};
		if (FAILED(device->SetVertexShaderConstantF(0, packet.viewProjection[0], 4)) ||
			FAILED(device->SetVertexShaderConstantF(5, packet.eye, 1)) ||
			FAILED(device->SetPixelShader(s_Resources.pixels[params.iEffect - 1].Get())) ||
			FAILED(device->SetPixelShaderConstantF(0, constants[0], 4)))
			return false;
		bool success = true;
		auto state = [&](D3DRENDERSTATETYPE type, DWORD value)
		{
			success = SUCCEEDED(device->SetRenderState(type, value)) && success;
		};
		state(D3DRS_ZENABLE, D3DZB_TRUE);
		state(D3DRS_ZFUNC, packet.occluded ? D3DCMP_GREATER : D3DCMP_LESSEQUAL);
		state(D3DRS_ZWRITEENABLE, params.bDepthWrite && !packet.occluded);
		state(D3DRS_ALPHATESTENABLE, FALSE);
		state(D3DRS_ALPHABLENDENABLE, TRUE);
		state(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
		state(D3DRS_BLENDOP, D3DBLENDOP_ADD);
		state(D3DRS_SRCBLEND, params.iBlend == 2 ? D3DBLEND_ONE : D3DBLEND_SRCALPHA);
		state(D3DRS_DESTBLEND, params.iBlend == 1 ? D3DBLEND_ONE : params.iBlend == 2 ? D3DBLEND_INVSRCCOLOR : D3DBLEND_INVSRCALPHA);
		state(D3DRS_FILLMODE, params.iEffect == 10 ? D3DFILL_WIREFRAME : D3DFILL_SOLID);
		state(D3DRS_FOGENABLE, FALSE);
		state(D3DRS_LIGHTING, FALSE);
		state(D3DRS_SRGBWRITEENABLE, FALSE);
		state(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
		state(D3DRS_DEPTHBIAS, 0);
		state(D3DRS_SLOPESCALEDEPTHBIAS, 0);
		if (!success)
			return false;
		for (const auto& group : packet.groups)
		{
			const auto vertices = group.mesh->vertices;
			matrix3x4 palette[HardwareBones];
			for (auto& matrix : palette)
				Identity(matrix);
			if (!group.skinned)
				std::memcpy(palette[0], packet.poses[0], sizeof(matrix3x4));
			const bool compressed = (group.format & VERTEX_FORMAT_COMPRESSED) != 0;
			const float decode[4] = { 0.f, compressed ? 1.f / 32768.f : 1.f, compressed ? 1.f / 32768.f : 0.f, compressed ? 1.f : 0.f };
			if (FAILED(device->SetVertexDeclaration(s_Resources.declarations[group.declaration].Get())) ||
				FAILED(device->SetVertexShader(group.skinned ? s_Resources.skinned.Get() : s_Resources.rigid.Get())) ||
				FAILED(device->SetVertexShaderConstantF(4, decode, 1)) ||
				FAILED(device->SetVertexShaderConstantF(PaletteRegister, palette[0][0], HardwareBones * 3)) ||
				FAILED(device->SetStreamSourceFreq(0, 1)) ||
				FAILED(device->SetStreamSource(0, vertices->buffer, 0, vertices->stride)) ||
				FAILED(device->SetIndices(group.mesh->indices->buffer)))
				return false;
			for (const auto& strip : group.strips)
			{
				for (const auto& change : strip.changes)
				{
					if (FAILED(device->SetVertexShaderConstantF(PaletteRegister + change.hardwareID * 3, packet.poses[change.newBoneID][0], 3)))
						return false;
				}
				if (FAILED(device->DrawIndexedPrimitive(strip.triangleStrip ? D3DPT_TRIANGLESTRIP : D3DPT_TRIANGLELIST,
					0, 0, vertices->count, strip.start, strip.triangleStrip ? strip.count - 2 : strip.count / 3)))
					return false;
			}
		}
		return true;
	}
}

void CD3DMaterials::Initialize(IDirect3DDevice9* pDevice)
{
	if (I::MaterialSystem && I::MaterialSystem->GetRenderBackend() == RENDER_BACKEND_D3D9)
		InitializeResources(pDevice, s_Generation.load(std::memory_order_acquire));
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
		const auto generation = s_Generation.load(std::memory_order_acquire);
		if (s_ReadyGeneration.load(std::memory_order_acquire) != generation)
		{
			if (I::DirectXDevice && s_InitializationGeneration.load(std::memory_order_acquire) != generation)
			{
				auto expected = InvalidGeneration;
				if (s_WarmupGeneration.compare_exchange_strong(expected, generation, std::memory_order_acq_rel))
				{
					if (!QueueRenderCall(context, [device = I::DirectXDevice, generation] { InitializeResources(device, generation); }))
					{
						expected = generation;
						s_WarmupGeneration.compare_exchange_strong(expected, InvalidGeneration, std::memory_order_acq_rel);
					}
				}
			}
			return false;
		}
		Packet packet;
		packet.generation = generation;
		packet.params = params;
		packet.occluded = bOccluded;
		packet.time = I::GlobalVars ? I::GlobalVars->curtime : 0.f;
		if (!std::isfinite(packet.time) || !CopyModel(state, info, bones, packet))
			return false;
		VMatrix view, projection;
		context->GetMatrix(MATERIAL_VIEW, &view);
		context->GetMatrix(MATERIAL_PROJECTION, &projection);
		for (int row = 0; row < 4; ++row)
		{
			for (int column = 0; column < 4; ++column)
			{
				packet.viewProjection[row][column] = projection[row][0] * view[0][column] + projection[row][1] * view[1][column] +
					projection[row][2] * view[2][column] + projection[row][3] * view[3][column];
				if (!std::isfinite(packet.viewProjection[row][column]))
					return false;
			}
		}
		for (int axis = 0; axis < 3; ++axis)
		{
			packet.eye[axis] = -(view[0][axis] * view[0][3] + view[1][axis] * view[1][3] + view[2][axis] * view[2][3]);
			if (!std::isfinite(packet.eye[axis]))
				return false;
		}
		packet.eye[3] = 1.f;
		DrawModelExecuteFn original = nullptr;
		if (packet.needsFallback)
		{
			static auto hook = U::Hooks.Get("IVModelRender_DrawModelExecute");
			original = hook ? hook->As<DrawModelExecuteFn>() : nullptr;
			if (!original || !I::ModelRender)
				return false;
		}
		if (auto queue = context->GetCallQueue())
		{
			const bool needsFallback = packet.needsFallback;
			auto call = [packet = std::move(packet)] { Render(packet); };
			auto functor = new (std::nothrow) RenderQueue::CRenderCall<decltype(call)>(std::move(call));
			if (!functor)
				return false;
			const auto releaseFunctor = [](CFunctor* value) { value->Release(); };
			const std::unique_ptr<CFunctor, decltype(releaseFunctor)> owner(functor, releaseFunctor);
			if (needsFallback)
			{
				original(I::ModelRender, state, info, const_cast<matrix3x4*>(bones));
				drewFallback = true;
			}
			context->Flush(false);
			queue->QueueFunctor(functor);
			return true;
		}
		if (packet.needsFallback)
		{
			original(I::ModelRender, state, info, const_cast<matrix3x4*>(bones));
			drewFallback = true;
		}
		context->Flush(false);
		const bool rendered = Render(packet);
		return rendered || drewFallback;
	}
	catch (const std::bad_alloc&)
	{
		return drewFallback;
	}
}

void CD3DMaterials::Unload()
{
	std::scoped_lock lock(s_Mutex);
	s_ReadyGeneration.store(InvalidGeneration, std::memory_order_release);
	s_Generation.fetch_add(1, std::memory_order_acq_rel);
	s_InitializationGeneration.store(InvalidGeneration, std::memory_order_release);
	s_WarmupGeneration.store(InvalidGeneration, std::memory_order_release);
	s_Resources = {};
}
#else
void CD3DMaterials::Initialize(IDirect3DDevice9*)
{
}

bool CD3DMaterials::Draw(const DrawModelState_t&, const ModelRenderInfo_t&, const matrix3x4*, const ChamsMaterial_t&, bool, bool)
{
	return false;
}

void CD3DMaterials::Unload()
{
}
#endif

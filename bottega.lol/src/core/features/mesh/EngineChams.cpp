/*
 * Native (engine) chams - ported from phantomX paid.
 * Roblox's OWN renderer draws these: we patch every FastClusterEntity
 * (render-queue + material layers) of the tracked instances every tick, so
 * the chams are pixel-perfect by construction - they can never detach from
 * the avatar, lag behind it or mix with the overlay swapchain.
 * 100% external (ReadProcessMemory / WriteProcessMemory).
 */
#include "EngineChams.h"

#include "memory/memory.h"
#include "sdk/offsets.h"
#include "sdk/sdk.h"
#include "core/globals/globals.h"
#include "core/cache/cache.h"
#include "core/variables/variables.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef GetClassName
#undef GetClassName
#endif

namespace EngineChams {

namespace {

std::thread g_thread;
std::atomic<bool> g_run{false};

struct LayerBackup {
	uint8_t fillmode = 0;
	uint32_t matflags = 0;
	uint32_t param = 0;
	uint32_t flags2 = 0;
	uint32_t color = 0;
};

std::mutex g_mtx;
std::unordered_map<uintptr_t, uint32_t> g_saved;
std::unordered_map<uintptr_t, uint8_t> g_alpha;
std::unordered_map<uintptr_t, LayerBackup> g_layers;
std::unordered_map<uintptr_t, std::vector<uintptr_t>> g_ent_layers;
std::unordered_map<uintptr_t, int> g_miss;
uintptr_t g_vt = 0;
int g_applied_style = -1;

static constexpr int k_style_max = 7;
static constexpr int k_scan_every = 2;
static constexpr SIZE_T k_scan_chunk = 16ull * 1024ull * 1024ull;
static constexpr std::size_t k_max_tracked = 4096;

HANDLE Proc()
{
	HANDLE h = memory->get_process_handle();
	if (!h || h == INVALID_HANDLE_VALUE)
		return nullptr;
	return h;
}

bool AddrValid(uintptr_t addr)
{
	if (!addr)
		return false;
	HANDLE h = Proc();
	if (!h)
		return false;
	MEMORY_BASIC_INFORMATION mbi{};
	if (!VirtualQueryEx(h, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)))
		return false;
	if (mbi.State != MEM_COMMIT)
		return false;
	if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
		return false;
	const DWORD p = mbi.Protect & ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
	return p == PAGE_READWRITE || p == PAGE_EXECUTE_READWRITE ||
	       p == PAGE_WRITECOPY || p == PAGE_EXECUTE_WRITECOPY ||
	       p == PAGE_READONLY || p == PAGE_EXECUTE_READ;
}

bool AddrWritable(uintptr_t addr, size_t n)
{
	if (!addr)
		return false;
	HANDLE h = Proc();
	if (!h)
		return false;
	MEMORY_BASIC_INFORMATION mbi{};
	if (!VirtualQueryEx(h, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)))
		return false;
	if (mbi.State != MEM_COMMIT)
		return false;
	const DWORD p = mbi.Protect & ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
	return p == PAGE_READWRITE || p == PAGE_EXECUTE_READWRITE ||
	       p == PAGE_WRITECOPY || p == PAGE_EXECUTE_WRITECOPY;
}

bool EntAlive(uintptr_t ent)
{
	if (!g_vt || !AddrValid(ent) || !AddrValid(ent + 8))
		return false;

	if (memory->read<uintptr_t>(ent) != g_vt)
		return false;

	return AddrWritable(ent + Offsets::FastClusterEntity::RenderQueueId, sizeof(uint32_t));
}

bool EntKnown(uintptr_t ent)
{
	if (!g_vt || !AddrValid(ent))
		return false;

	return memory->read<uintptr_t>(ent) == g_vt;
}

bool LayerAlive(uintptr_t layer)
{
	return AddrWritable(layer + Offsets::MaterialLayer::FillModeByte, 1) &&
	       AddrWritable(layer + Offsets::MaterialLayer::ColorData, sizeof(uint32_t));
}

void DropLayersLocked(uintptr_t ent)
{
	auto lit = g_ent_layers.find(ent);
	if (lit == g_ent_layers.end())
		return;

	for (uintptr_t layer : lit->second)
		g_layers.erase(layer);
	g_ent_layers.erase(lit);
}

void RestoreLayersLocked(uintptr_t ent)
{
	auto lit = g_ent_layers.find(ent);
	if (lit == g_ent_layers.end())
		return;

	const bool alive = EntAlive(ent);

	for (uintptr_t layer : lit->second)
	{
		auto bit = g_layers.find(layer);
		if (bit == g_layers.end())
			continue;

		if (alive && LayerAlive(layer))
		{
			LayerBackup& b = bit->second;
			memory->write<uint8_t>(layer + Offsets::MaterialLayer::FillModeByte, b.fillmode);
			if (AddrWritable(layer + Offsets::MaterialLayer::MatFlags, 4))
				memory->write<uint32_t>(layer + Offsets::MaterialLayer::MatFlags, b.matflags);
			if (AddrWritable(layer + Offsets::MaterialLayer::Param, 4))
				memory->write<uint32_t>(layer + Offsets::MaterialLayer::Param, b.param);
			if (AddrWritable(layer + Offsets::MaterialLayer::Flags2, 4))
				memory->write<uint32_t>(layer + Offsets::MaterialLayer::Flags2, b.flags2);
			memory->write<uint32_t>(layer + Offsets::MaterialLayer::ColorData, b.color);
		}

		g_layers.erase(bit);
	}
	g_ent_layers.erase(lit);
}

void RestoreAll()
{
	std::unordered_map<uintptr_t, uint32_t> saved;
	std::unordered_map<uintptr_t, uint8_t> alpha;
	std::unordered_map<uintptr_t, std::vector<uintptr_t>> ent_layers;
	std::unordered_map<uintptr_t, LayerBackup> layers;
	{
		std::lock_guard<std::mutex> lk(g_mtx);
		saved.swap(g_saved);
		alpha.swap(g_alpha);
		ent_layers.swap(g_ent_layers);
		layers.swap(g_layers);
		g_miss.clear();
		g_applied_style = -1;
	}

	for (const auto& [ent, id] : saved)
	{
		if (!EntAlive(ent))
			continue;

		memory->write<uint32_t>(ent + Offsets::FastClusterEntity::RenderQueueId, id);
		auto ait = alpha.find(ent);
		if (ait != alpha.end())
		{
			uintptr_t ap = ent + Offsets::FastClusterEntity::AlphaByte;
			if (AddrWritable(ap, 1))
				memory->write<uint8_t>(ap, ait->second);
		}

		auto lit = ent_layers.find(ent);
		if (lit == ent_layers.end())
			continue;

		for (uintptr_t layer : lit->second)
		{
			auto bit = layers.find(layer);
			if (bit == layers.end() || !LayerAlive(layer))
				continue;

			LayerBackup& b = bit->second;
			memory->write<uint8_t>(layer + Offsets::MaterialLayer::FillModeByte, b.fillmode);
			if (AddrWritable(layer + Offsets::MaterialLayer::MatFlags, 4))
				memory->write<uint32_t>(layer + Offsets::MaterialLayer::MatFlags, b.matflags);
			if (AddrWritable(layer + Offsets::MaterialLayer::Param, 4))
				memory->write<uint32_t>(layer + Offsets::MaterialLayer::Param, b.param);
			if (AddrWritable(layer + Offsets::MaterialLayer::Flags2, 4))
				memory->write<uint32_t>(layer + Offsets::MaterialLayer::Flags2, b.flags2);
			memory->write<uint32_t>(layer + Offsets::MaterialLayer::ColorData, b.color);
		}
	}
}

void RestoreOne(uintptr_t ent)
{
	if (!EntAlive(ent))
	{
		std::lock_guard<std::mutex> lk(g_mtx);
		g_saved.erase(ent);
		g_alpha.erase(ent);
		DropLayersLocked(ent);
		return;
	}

	std::lock_guard<std::mutex> lk(g_mtx);
	auto it = g_saved.find(ent);
	if (it != g_saved.end())
	{
		memory->write<uint32_t>(ent + Offsets::FastClusterEntity::RenderQueueId, it->second);
		g_saved.erase(it);
	}
	auto ait = g_alpha.find(ent);
	if (ait != g_alpha.end())
	{
		uintptr_t ap = ent + Offsets::FastClusterEntity::AlphaByte;
		if (AddrWritable(ap, 1))
			memory->write<uint8_t>(ap, ait->second);
		g_alpha.erase(ait);
	}
	RestoreLayersLocked(ent);
}

bool WorldCenter(uintptr_t ent, RBX::Vec3& out)
{
	namespace FC = Offsets::FastClusterEntity;

	float minx = memory->read<float>(ent + FC::BBoxMinX);
	float miny = memory->read<float>(ent + FC::BBoxMinY);
	float minz = memory->read<float>(ent + FC::BBoxMinZ);
	float maxx = memory->read<float>(ent + FC::BBoxMaxX);
	float maxy = memory->read<float>(ent + FC::BBoxMaxY);
	float maxz = memory->read<float>(ent + FC::BBoxMaxZ);
	if (!std::isfinite(minx) || !std::isfinite(maxx))
		return false;

	RBX::Vec3 local{
		(minx + maxx) * 0.5f,
		(miny + maxy) * 0.5f,
		(minz + maxz) * 0.5f};

	uintptr_t ctx = memory->read<uintptr_t>(ent + FC::ContextPtr);
	if (!AddrValid(ctx))
		return false;

	uintptr_t pool = memory->read<uintptr_t>(ctx + FC::ContextPrimitivePoolPtr);
	if (!AddrValid(pool))
		return false;

	uintptr_t base = memory->read<uintptr_t>(pool + FC::PrimitivePoolArrayBase);
	if (!AddrValid(base))
		return false;

	uintptr_t idx_ptr = memory->read<uintptr_t>(ent + FC::PrimitiveIndexArrayPtr);
	if (!AddrValid(idx_ptr))
		return false;

	uint32_t idx = memory->read<uint32_t>(idx_ptr);
	if (idx > 1000000u)
		return false;

	uintptr_t record = base + FC::PrimitiveRecordStride * (uintptr_t)idx;
	if (!AddrValid(record))
		return false;

	float m[9]{};
	if (!memory->read_raw(record, m, sizeof(m)))
		return false;

	RBX::Vec3 t = memory->read<RBX::Vec3>(record + FC::PrimitiveRecordTranslation);
	out.X = m[0] * local.X + m[1] * local.Y + m[2] * local.Z + t.X;
	out.Y = m[3] * local.X + m[4] * local.Y + m[5] * local.Z + t.Y;
	out.Z = m[6] * local.X + m[7] * local.Y + m[8] * local.Z + t.Z;
	return std::isfinite(out.X) && std::isfinite(out.Y) && std::isfinite(out.Z);
}

bool PushPartAddr(uintptr_t part, std::vector<RBX::Vec3>& out)
{
	if (!part || !AddrValid(part))
		return false;

	uintptr_t prim = memory->read<uintptr_t>(part + Offsets::BasePart::Primitive);
	if (!AddrValid(prim))
		return false;

	RBX::Vec3 p = memory->read<RBX::Vec3>(prim + Offsets::Primitive::Position);
	if (!std::isfinite(p.X) || !std::isfinite(p.Y) || !std::isfinite(p.Z))
		return false;

	out.push_back(p);
	return true;
}

bool IsPartClass(const std::string& cls)
{
	if (cls == "Part") return true;
	if (cls == "MeshPart") return true;
	if (cls == "WedgePart") return true;
	if (cls == "CornerWedgePart") return true;
	if (cls == "TrussPart") return true;
	if (cls == "UnionOperation") return true;
	if (cls == "PartOperation") return true;
	if (cls == "Handle") return true;
	return false;
}

void PushKidsParts(const RBX::RbxInstance& parent, std::vector<RBX::Vec3>& out, int depth)
{
	if (depth < 0 || !AddrValid(parent.Addr))
		return;

	for (const auto& ch : parent.GetChildList())
	{
		if (!AddrValid(ch.Addr))
			continue;

		const std::string cls = ch.GetClass();
		if (IsPartClass(cls))
		{
			PushPartAddr(ch.Addr, out);
		}
		else if (depth > 0 &&
		         (cls == "Model" || cls == "Folder" || cls == "Accessory" ||
		          cls == "Tool" || cls == "Accoutrement"))
		{
			PushKidsParts(ch, out, depth - 1);
		}
	}
}

bool GetLocalPartCenters(std::vector<RBX::Vec3>& out)
{
	out.clear();

	if (!Globals::players.Addr)
		return false;

	const uintptr_t lp = memory->read<uintptr_t>(
		Globals::players.Addr + Offsets::Players::LocalPlayer);
	if (!AddrValid(lp))
		return false;

	RBX::RbxInstance local(lp);
	auto ch = local.GetModelRef();
	if (!AddrValid(ch.Addr))
		ch = RBX::RbxInstance(memory->read<uintptr_t>(lp + Offsets::Player::ModelInstance));
	if (!AddrValid(ch.Addr))
		return false;

	const auto& l = PlayerCache::GetLimbs(ch.Addr);
	PushPartAddr(l.head, out);
	PushPartAddr(l.hrp, out);
	PushPartAddr(l.torso, out);
	PushPartAddr(l.upperTorso, out);
	PushPartAddr(l.lowerTorso, out);
	PushPartAddr(l.lUpperArm, out);
	PushPartAddr(l.lLowerArm, out);
	PushPartAddr(l.lHand, out);
	PushPartAddr(l.rUpperArm, out);
	PushPartAddr(l.rLowerArm, out);
	PushPartAddr(l.rHand, out);
	PushPartAddr(l.lUpperLeg, out);
	PushPartAddr(l.lLowerLeg, out);
	PushPartAddr(l.lFoot, out);
	PushPartAddr(l.rUpperLeg, out);
	PushPartAddr(l.rLowerLeg, out);
	PushPartAddr(l.rFoot, out);
	PushPartAddr(l.lArm, out);
	PushPartAddr(l.rArm, out);
	PushPartAddr(l.lLeg, out);
	PushPartAddr(l.rLeg, out);
	PushKidsParts(ch, out, 2);

	return !out.empty();
}

float MinDistSq(const RBX::Vec3& p, const std::vector<RBX::Vec3>& anchors)
{
	float best = 1.0e30f;
	for (const RBX::Vec3& a : anchors)
	{
		const float dx = p.X - a.X;
		const float dy = p.Y - a.Y;
		const float dz = p.Z - a.Z;
		const float d = dx * dx + dy * dy + dz * dz;
		if (d < best)
			best = d;
	}
	return best;
}

void GetOtherRoots(std::vector<RBX::Vec3>& out, std::uintptr_t local_addr)
{
	out.clear();
	const auto snap = PlayerCache::SnapshotPlayers();
	for (const auto& p : *snap)
	{
		if (!p.isValid || p.playerAddr == local_addr)
			continue;

		if (p.rootPartAddr)
		{
			const std::uintptr_t prim = memory->read<std::uintptr_t>(p.rootPartAddr + Offsets::BasePart::Primitive);
			if (AddrValid(prim))
			{
				const RBX::Vec3 pos = memory->read<RBX::Vec3>(prim + Offsets::Primitive::Position);
				if (std::isfinite(pos.X) && std::isfinite(pos.Y) && std::isfinite(pos.Z))
					out.push_back(pos);
			}
		}
		if (p.headAddr)
		{
			const std::uintptr_t prim = memory->read<std::uintptr_t>(p.headAddr + Offsets::BasePart::Primitive);
			if (AddrValid(prim))
			{
				const RBX::Vec3 pos = memory->read<RBX::Vec3>(prim + Offsets::Primitive::Position);
				if (std::isfinite(pos.X) && std::isfinite(pos.Y) && std::isfinite(pos.Z))
					out.push_back(pos);
			}
		}
	}
}

bool IsLocalEntity(uintptr_t ent, const std::vector<RBX::Vec3>& local_a,
                   const std::vector<RBX::Vec3>& other_a)
{
	if (local_a.empty())
		return false;

	RBX::Vec3 p{};
	if (!WorldCenter(ent, p))
		return false;

	const float dl = MinDistSq(p, local_a);
	if (dl > 6.f * 6.f)
		return false;

	if (!other_a.empty())
	{
		const float dout = MinDistSq(p, other_a);
		if (dout + 0.35f < dl)
			return false;
	}

	return true;
}

uint32_t ColorParam(int idx)
{
	if (idx < 0)
		idx = 0;
	if (idx > 6)
		idx = 6;
	return (uint32_t)(idx + 1);
}

bool StyleUsesPicker(int style)
{
	if (style == 1) return true;
	if (style == 2) return true;
	if (style == 5) return true;
	if (style == 6) return true;
	return false;
}

uint32_t PackColorData(const float c[4])
{
	float r = c[0];
	float g = c[1];
	float b = c[2];
	if (r < 0.f) r = 0.f;
	if (r > 1.f) r = 1.f;
	if (g < 0.f) g = 0.f;
	if (g > 1.f) g = 1.f;
	if (b < 0.f) b = 0.f;
	if (b > 1.f) b = 1.f;
	const uint32_t rr = (uint32_t)(r * 255.f + 0.5f);
	const uint32_t gg = (uint32_t)(g * 255.f + 0.5f);
	const uint32_t bb = (uint32_t)(b * 255.f + 0.5f);
	return (0xFFu << 24) | (rr << 16) | (gg << 8) | bb;
}

int NearestColorIdx(const float c[4])
{
	static const float tab[7][3] = {
		{ 1.f, 0.f, 0.f },
		{ 0.f, 1.f, 0.f },
		{ 1.f, 0.50f, 0.f },
		{ 0.f, 0.50f, 1.f },
		{ 1.f, 0.f, 1.f },
		{ 0.f, 1.f, 1.f },
		{ 1.f, 1.f, 1.f },
	};

	float best = 1.0e30f;
	int bi = 6;

	for (int i = 0; i < 7; ++i)
	{
		const float dr = c[0] - tab[i][0];
		const float dg = c[1] - tab[i][1];
		const float db = c[2] - tab[i][2];
		const float d = dr * dr + dg * dg + db * db;
		if (d < best)
		{
			best = d;
			bi = i;
		}
	}
	return bi;
}

uint8_t PackAlphaByte(const float c[4])
{
	float a = c[3];
	if (a < 0.f) a = 0.f;
	if (a > 1.f) a = 1.f;
	uint8_t v = (uint8_t)(a * 255.f + 0.5f);
	if (v < 40)
		v = 40;
	return v;
}

uint32_t StyleQueue(int style)
{
	namespace RQ = Offsets::RenderQueue;

	if (style == 4)
		return RQ::Glass;

	if (style == 5)
		return RQ::GlassTint;

	if (style == 6)
		return RQ::Transparent;

	if (style == 7)
		return RQ::OnTopWithDepth;

	return RQ::AlwaysOnTop;
}

void ApplyLayers(uintptr_t ent, uint8_t fill, uint32_t param, uint32_t flags2, uint32_t color)
{
	if (!EntAlive(ent))
		return;

	uintptr_t arr = memory->read<uintptr_t>(ent + Offsets::FastClusterEntity::TechniqueArrayPtr);
	if (!AddrValid(arr))
		return;

	uintptr_t begin = memory->read<uintptr_t>(arr + Offsets::TechniqueArray::BeginOffset);
	uintptr_t end = memory->read<uintptr_t>(arr + Offsets::TechniqueArray::EndOffset);
	if (!AddrValid(begin) || end <= begin)
		return;

	std::size_t bytes = (std::size_t)(end - begin);
	if (bytes > 64ull * 1024ull)
		return;

	std::size_t count = bytes / Offsets::MaterialLayer::Stride;
	if (count == 0 || count > 256)
		return;

	for (std::size_t i = 0; i < count; ++i)
	{
		uintptr_t layer = begin + i * Offsets::MaterialLayer::Stride;
		if (!LayerAlive(layer))
			continue;

		{
			std::lock_guard<std::mutex> lk(g_mtx);
			if (!g_layers.count(layer))
			{
				LayerBackup b{};
				b.fillmode = memory->read<uint8_t>(layer + Offsets::MaterialLayer::FillModeByte);
				b.matflags = memory->read<uint32_t>(layer + Offsets::MaterialLayer::MatFlags);
				b.param = memory->read<uint32_t>(layer + Offsets::MaterialLayer::Param);
				b.flags2 = memory->read<uint32_t>(layer + Offsets::MaterialLayer::Flags2);
				b.color = memory->read<uint32_t>(layer + Offsets::MaterialLayer::ColorData);
				g_layers[layer] = b;
				g_ent_layers[ent].push_back(layer);
			}
		}

		memory->write<uint8_t>(layer + Offsets::MaterialLayer::FillModeByte, fill);
		if (AddrWritable(layer + Offsets::MaterialLayer::MatFlags, 4))
			memory->write<uint32_t>(layer + Offsets::MaterialLayer::MatFlags, 0);
		if (AddrWritable(layer + Offsets::MaterialLayer::Param, 4))
			memory->write<uint32_t>(layer + Offsets::MaterialLayer::Param, param);
		if (AddrWritable(layer + Offsets::MaterialLayer::Flags2, 4))
			memory->write<uint32_t>(layer + Offsets::MaterialLayer::Flags2, flags2);
		memory->write<uint32_t>(layer + Offsets::MaterialLayer::ColorData, color);
	}
}

bool ApplyStyleLayers(uintptr_t ent, int style)
{
	const float* pc = variables::ESP::engineChamsColor;
	int dropdown_idx = variables::ESP::engineGhostColorIdx;

	if (style == 1)
	{
		// ghost: color via Param, ColorData white
		ApplyLayers(ent, 0, ColorParam(NearestColorIdx(pc)), 0u, 0xFFFFFFFFu);
		return true;
	}
	if (style == 2)
	{
		// simple wireframe
		ApplyLayers(ent, 1, ColorParam(NearestColorIdx(pc)), 0u, 0xFFFFFFFFu);
		return true;
	}
	if (style == 3)
	{
		// colored frame - dropdown
		ApplyLayers(ent, 1, ColorParam(dropdown_idx), 7u, 0xFFFFFFFFu);
		return true;
	}
	if (style == 4)
	{
		// colored - dropdown
		ApplyLayers(ent, 0, ColorParam(dropdown_idx), 15u, 0xFFFFFFFFu);
		return true;
	}
	if (style == 5)
	{
		// smoke no shadow
		ApplyLayers(ent, 0, ColorParam(NearestColorIdx(pc)), 0u, 0xFFFFFFFFu);
		return true;
	}
	if (style == 6)
	{
		// smoke
		ApplyLayers(ent, 0, ColorParam(NearestColorIdx(pc)), 0u, 0xFFFFFFFFu);
		return true;
	}
	if (style == 7)
	{
		// invisible
		ApplyLayers(ent, 0, ColorParam(6), 0u, 0xFFFFFFFFu);
		return true;
	}
	return false;
}

void ApplyEntity(uintptr_t ent)
{
	if (!EntKnown(ent))
		return;

	uintptr_t rq = ent + Offsets::FastClusterEntity::RenderQueueId;
	uintptr_t ap = ent + Offsets::FastClusterEntity::AlphaByte;
	if (!AddrWritable(rq, sizeof(uint32_t)))
		return;

	{
		std::lock_guard<std::mutex> lk(g_mtx);
		if (!g_saved.count(ent))
		{
			g_saved[ent] = memory->read<uint32_t>(rq);
			g_alpha[ent] = memory->read<uint8_t>(ap);
		}
	}

	int style = variables::ESP::engineChamsStyle;
	if (style < 0)
		style = 0;
	if (style > k_style_max)
		style = k_style_max;

	// the engine resets the queue - write every tick. alpha untouched
	memory->write<uint32_t>(rq, StyleQueue(style));

	{
		std::lock_guard<std::mutex> lk(g_mtx);
		auto ait = g_alpha.find(ent);
		if (ait != g_alpha.end() && AddrWritable(ap, 1))
			memory->write<uint8_t>(ap, ait->second);
	}

	if (ApplyStyleLayers(ent, style))
		return;

	std::lock_guard<std::mutex> lk(g_mtx);
	if (g_ent_layers.count(ent))
		RestoreLayersLocked(ent);
}

void DropDeadLocked(uintptr_t ent)
{
	g_saved.erase(ent);
	g_alpha.erase(ent);
	DropLayersLocked(ent);
	g_miss.erase(ent);
}

void RefreshKnown()
{
	int style = variables::ESP::engineChamsStyle;

	std::vector<uintptr_t> ents;
	{
		std::lock_guard<std::mutex> lk(g_mtx);
		ents.reserve(g_saved.size());
		for (const auto& [ent, _] : g_saved)
			ents.push_back(ent);
	}

	const bool had_layers = g_applied_style >= 1 && g_applied_style <= 7;
	const bool want_layers = style >= 1 && style <= 7;
	if (had_layers && !want_layers)
	{
		for (uintptr_t ent : ents)
		{
			if (!EntKnown(ent))
				continue;
			std::lock_guard<std::mutex> lk(g_mtx);
			RestoreLayersLocked(ent);
		}
	}

	const bool skip_local = !variables::ESP::localPlayer;
	std::vector<RBX::Vec3> local_a;
	std::vector<RBX::Vec3> other_a;
	bool have_local = false;
	if (skip_local)
	{
		have_local = GetLocalPartCenters(local_a);
		if (have_local)
			GetOtherRoots(other_a, Globals::localPlayer.Addr);
	}

	for (uintptr_t ent : ents)
	{
		if (!EntKnown(ent))
		{
			std::lock_guard<std::mutex> lk(g_mtx);
			int& n = g_miss[ent];
			++n;
			if (n >= 25)
				DropDeadLocked(ent);
			continue;
		}

		{
			std::lock_guard<std::mutex> lk(g_mtx);
			g_miss[ent] = 0;
		}

		if (skip_local && !have_local)
			continue;

		if (skip_local && IsLocalEntity(ent, local_a, other_a))
		{
			RestoreOne(ent);
			continue;
		}

		ApplyEntity(ent);
	}

	g_applied_style = style;
}

bool DiscoverEntity(uintptr_t ent)
{
	if (!EntKnown(ent))
		return false;

	if (!AddrWritable(ent + Offsets::FastClusterEntity::RenderQueueId, sizeof(uint32_t)))
		return false;

	std::lock_guard<std::mutex> lk(g_mtx);
	if (g_saved.count(ent) || g_saved.size() >= k_max_tracked)
		return false;
	return true;
}

void ScanOnce(uintptr_t vt)
{
	HANDLE proc = Proc();
	if (!proc)
		return;

	g_vt = vt;

	const bool skip_local = !variables::ESP::localPlayer;
	std::vector<RBX::Vec3> local_a;
	std::vector<RBX::Vec3> other_a;
	bool have_local = GetLocalPartCenters(local_a);
	if (have_local)
		GetOtherRoots(other_a, Globals::localPlayer.Addr);

	SYSTEM_INFO si{};
	GetSystemInfo(&si);

	uintptr_t max_va = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
	uintptr_t addr = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);

	static std::vector<uint8_t> buf;
	if (buf.size() < k_scan_chunk)
		buf.resize(k_scan_chunk);
	MEMORY_BASIC_INFORMATION mbi{};

	while (addr < max_va)
	{
		if (VirtualQueryEx(proc, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == 0)
			break;

		bool readable =
			mbi.Protect == PAGE_READWRITE ||
			mbi.Protect == PAGE_EXECUTE_READWRITE ||
			mbi.Protect == PAGE_WRITECOPY ||
			mbi.Protect == PAGE_EXECUTE_WRITECOPY;

		bool ok =
			mbi.State == MEM_COMMIT &&
			mbi.Type != MEM_IMAGE &&
			readable;

		if (ok && mbi.RegionSize > 0 && mbi.RegionSize <= 512ull * 1024ull * 1024ull)
		{
			uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
			SIZE_T sz = mbi.RegionSize;

			SIZE_T off = 0;
			while (off < sz)
			{
				SIZE_T chunk = (sz - off < k_scan_chunk) ? (sz - off) : k_scan_chunk;

				SIZE_T got = 0;
				if (ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(base + off),
				                      buf.data(), chunk, &got) &&
				    got >= 16)
				{
					for (SIZE_T i = 0; i + 16 <= got; i += 8)
					{
						if (*reinterpret_cast<const uintptr_t*>(buf.data() + i) != vt)
							continue;

						uintptr_t node =
							*reinterpret_cast<const uintptr_t*>(buf.data() + i + 8);
						if (node < 0x10000 || node >= 0x7FFFFFFEFFFFull)
							continue;

						uintptr_t ent = base + off + i;

						if (!DiscoverEntity(ent))
							continue;

						if (skip_local && !have_local)
							continue;

						if (skip_local && IsLocalEntity(ent, local_a, other_a))
							continue;

						ApplyEntity(ent);
					}
				}

				off += chunk;
			}
		}

		uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
		if (next <= addr)
			break;
		addr = next;
	}
}

bool Active()
{
	if (!variables::ESP::engineChams)
		return false;

	return memory->IsConnected() && Globals::players.Addr != 0;
}

void Loop()
{
	bool was_on = false;
	int tick = 0;
	uintptr_t base = 0;

	while (g_run.load(std::memory_order_relaxed))
	{
		try
		{
			bool on = Active();

			if (on)
			{
				if (!was_on)
				{
					std::lock_guard<std::mutex> lk(g_mtx);
					g_saved.clear();
					g_layers.clear();
					g_ent_layers.clear();
					g_miss.clear();
					g_applied_style = -1;
					was_on = true;
					tick = 0;
					base = 0;
				}

				if ((tick % k_scan_every) == 0)
				{
					base = memory->get_module_address();
					if (base)
						g_vt = base + Offsets::FastClusterEntity::VTableRva;
				}

				RefreshKnown();

				if ((tick % k_scan_every) == 0 && base)
					ScanOnce(g_vt);

				++tick;
			}
			else if (was_on)
			{
				RestoreAll();
				was_on = false;
				tick = 0;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(on ? 15 : 200));
		}
		catch (...)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(30));
		}
	}

	if (was_on)
		RestoreAll();
}

} // namespace

void Start()
{
	if (g_run.exchange(true))
		return;

	g_thread = std::thread(Loop);
}

void Stop()
{
	if (!g_run.exchange(false))
		return;

	if (g_thread.joinable())
		g_thread.join();
}

} // namespace EngineChams

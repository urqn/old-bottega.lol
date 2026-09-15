#pragma once
#include "../../../src/sdk/sdk.h"
#include "../globals/globals.h"
#include "../variables/variables.h"
#include <vector>
#include <string>
#include <utility>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>

namespace PlayerCache {
struct CachedPlayer {
    std::uintptr_t playerAddr = 0;
    std::uintptr_t characterAddr = 0;
    std::uintptr_t humanoidAddr = 0;
    std::uintptr_t rootPartAddr = 0;
    std::uintptr_t headAddr = 0;
    std::uintptr_t teamAddr = 0;
    std::string name;
    std::string tool = "None";
    RBX::Vec3 position{};
    RBX::Vec3 box_min{};
    RBX::Vec3 box_max{};
    bool have_box = false;
    std::uint64_t box_stamp = 0;
    float health = 0.0f;
    float maxHealth = 0.0f;
    float distance = 0.0f;
    bool isValid = false;
    bool isR6 = false;
};

struct LimbAddrs {
    bool r6 = false;
    std::uintptr_t head = 0;
    std::uintptr_t hrp = 0;
    std::uintptr_t torso = 0;
    std::uintptr_t upperTorso = 0;
    std::uintptr_t lowerTorso = 0;
    std::uintptr_t lUpperArm = 0;
    std::uintptr_t lLowerArm = 0;
    std::uintptr_t lHand = 0;
    std::uintptr_t rUpperArm = 0;
    std::uintptr_t rLowerArm = 0;
    std::uintptr_t rHand = 0;
    std::uintptr_t lUpperLeg = 0;
    std::uintptr_t lLowerLeg = 0;
    std::uintptr_t lFoot = 0;
    std::uintptr_t rUpperLeg = 0;
    std::uintptr_t rLowerLeg = 0;
    std::uintptr_t rFoot = 0;
    std::uintptr_t lArm = 0;
    std::uintptr_t rArm = 0;
    std::uintptr_t lLeg = 0;
    std::uintptr_t rLeg = 0;
    std::uintptr_t humanoid = 0;
};

inline std::unordered_map<std::uintptr_t, LimbAddrs> limbCache;
inline std::mutex limbs_mtx;

inline LimbAddrs GetLimbs(std::uintptr_t characterAddr) {
    {
        std::lock_guard<std::mutex> lk(limbs_mtx);
        auto it = limbCache.find(characterAddr);
        if (it != limbCache.end())
            return it->second;
    }
    const bool full = [&] {
        std::lock_guard<std::mutex> lk(limbs_mtx);
        return limbCache.size() >= 256;
    }();
    if (full) {
        std::lock_guard<std::mutex> lk(limbs_mtx);
        limbCache.clear();
    }
    LimbAddrs l{};
    RBX::RbxInstance ch{characterAddr};
    auto head = ch.FindChild("Head");
    auto hrp = ch.FindChild("HumanoidRootPart");
    l.head = head.Addr;
    l.hrp = hrp.Addr;
    auto torso = ch.FindChild("Torso");
    l.r6 = torso.Addr != 0;
    l.torso = torso.Addr;
    if (l.r6) {
        l.lArm = ch.FindChild("Left Arm").Addr;
        l.rArm = ch.FindChild("Right Arm").Addr;
        l.lLeg = ch.FindChild("Left Leg").Addr;
        l.rLeg = ch.FindChild("Right Leg").Addr;
    } else {
        l.upperTorso = ch.FindChild("UpperTorso").Addr;
        l.lowerTorso = ch.FindChild("LowerTorso").Addr;
        l.lUpperArm = ch.FindChild("LeftUpperArm").Addr;
        l.lLowerArm = ch.FindChild("LeftLowerArm").Addr;
        l.lHand = ch.FindChild("LeftHand").Addr;
        l.rUpperArm = ch.FindChild("RightUpperArm").Addr;
        l.rLowerArm = ch.FindChild("RightLowerArm").Addr;
        l.rHand = ch.FindChild("RightHand").Addr;
        l.lUpperLeg = ch.FindChild("LeftUpperLeg").Addr;
        l.lLowerLeg = ch.FindChild("LeftLowerLeg").Addr;
        l.lFoot = ch.FindChild("LeftFoot").Addr;
        l.rUpperLeg = ch.FindChild("RightUpperLeg").Addr;
        l.rLowerLeg = ch.FindChild("RightLowerLeg").Addr;
        l.rFoot = ch.FindChild("RightFoot").Addr;
    }
    l.humanoid = ch.FindChildByClass("Humanoid").Addr;
    std::lock_guard<std::mutex> lk(limbs_mtx);
    auto res = limbCache.emplace(characterAddr, l);
    return res.first->second;
}

inline void PruneLimbs(const std::unordered_set<std::uintptr_t>& alive) {
    std::lock_guard<std::mutex> lk(limbs_mtx);
    for (auto it = limbCache.begin(); it != limbCache.end();) {
        if (alive.find(it->first) == alive.end())
            it = limbCache.erase(it);
        else
            ++it;
    }
}

inline std::uint64_t now_ms() {
    using Clock = std::chrono::steady_clock;
    return (std::uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch()).count();
}

inline bool ComputeBodyBox(const LimbAddrs& l, RBX::Vec3& mn, RBX::Vec3& mx) {
    bool any = false;
    mn = RBX::Vec3{ 1e9f, 1e9f, 1e9f };
    mx = RBX::Vec3{ -1e9f, -1e9f, -1e9f };
    const std::uintptr_t addrs[] = {
        l.head, l.hrp, l.torso, l.upperTorso, l.lowerTorso,
        l.lUpperArm, l.lLowerArm, l.lHand, l.rUpperArm, l.rLowerArm, l.rHand,
        l.lUpperLeg, l.lLowerLeg, l.lFoot, l.rUpperLeg, l.rLowerLeg, l.rFoot,
        l.lArm, l.rArm, l.lLeg, l.rLeg
    };
    for (const std::uintptr_t a : addrs) {
        if (!a)
            continue;
        const std::uintptr_t prim = memory->read<std::uintptr_t>(a + Offsets::BasePart::Primitive);
        if (!prim)
            continue;
        const RBX::Vec3 pos = memory->read<RBX::Vec3>(prim + Offsets::Primitive::Position);
        const RBX::Vec3 sz = memory->read<RBX::Vec3>(prim + Offsets::Primitive::Size);
        if (sz.X <= 0.001f || sz.Y <= 0.001f || sz.Z <= 0.001f)
            continue;
        const RBX::Vec3 lo{ pos.X - sz.X * 0.5f, pos.Y - sz.Y * 0.5f, pos.Z - sz.Z * 0.5f };
        const RBX::Vec3 hi{ pos.X + sz.X * 0.5f, pos.Y + sz.Y * 0.5f, pos.Z + sz.Z * 0.5f };
        mn.X = (std::min)(mn.X, lo.X); mn.Y = (std::min)(mn.Y, lo.Y); mn.Z = (std::min)(mn.Z, lo.Z);
        mx.X = (std::max)(mx.X, hi.X); mx.Y = (std::max)(mx.Y, hi.Y); mx.Z = (std::max)(mx.Z, hi.Z);
        any = true;
    }
    return any;
}

inline std::vector<CachedPlayer> players;
inline std::mutex players_mtx;
inline std::shared_ptr<const std::vector<CachedPlayer>> players_snap =
    std::make_shared<const std::vector<CachedPlayer>>();

inline void PublishPlayers() {
    auto snap = std::make_shared<const std::vector<CachedPlayer>>(players);
    std::lock_guard<std::mutex> lk(players_mtx);
    players_snap = std::move(snap);
}

inline std::shared_ptr<const std::vector<CachedPlayer>> SnapshotPlayers() {
    std::lock_guard<std::mutex> lk(players_mtx);
    return players_snap;
}
inline RBX::Vec3 localPlayerPos{};
inline std::uintptr_t localPlayerTeam = 0;
inline std::uintptr_t localRootPrim = 0;

// Re-resuelve los servicios globales desde el Datamodel actual. El Datamodel se
// recrea al cambiar de lugar/reunir: sin esto todo apuntaria a memoria reciclada.
inline void heal_globals() {
    const auto base = memory->get_module_address();
    if (!base)
        return;
    const auto fake = memory->read<std::uintptr_t>(base + Offsets::FakeDataModel::Pointer);
    if (!fake)
        return;
    const auto dm = memory->read<std::uintptr_t>(fake + Offsets::FakeDataModel::RealDataModel);
    const auto ve = memory->read<std::uintptr_t>(base + Offsets::VisualEngine::Pointer);
    if (!dm || !ve)
        return;
    Globals::dataModel = RBX::RbxInstance{dm};
    Globals::renderEngine = RBX::RenderEngine{ve};
    Globals::workspace = Globals::dataModel.FindChildByClass("Workspace");
    Globals::players = Globals::dataModel.FindChildByClass("Players");
    Globals::camera = Globals::workspace.Addr ? Globals::workspace.FindChildByClass("Camera")
                                              : RBX::RbxInstance{};
    const auto local = Globals::players.Addr
                           ? memory->read<std::uintptr_t>(Globals::players.Addr + Offsets::Players::LocalPlayer)
                           : 0;
    Globals::localPlayer = RBX::RbxInstance{local};
}

inline void updateplayers() {
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();

    struct Publisher {
        ~Publisher() { PublishPlayers(); }
    } _publish;

    // pasada rapida (cada frame): siguen vivos posicion, caja y health de cada slot
    if (localRootPrim) {
        const RBX::Vec3 lp = memory->read<RBX::Vec3>(localRootPrim + Offsets::Primitive::Position);
        if (std::isfinite(lp.X) && std::isfinite(lp.Y) && std::isfinite(lp.Z))
            localPlayerPos = lp;
        for (auto& c : players) {
            if (!c.isValid)
                continue;
            bool updated = false;
            if (c.rootPartAddr) {
                const RBX::Vec3 pos = RBX::RbxInstance(c.rootPartAddr).GetPos();
                if (std::isfinite(pos.X) && std::isfinite(pos.Y) && std::isfinite(pos.Z)) {
                    c.position = pos;
                    updated = true;
                    if (now_ms() - c.box_stamp > 150) {
                        const auto& lb = GetLimbs(c.characterAddr);
                        RBX::Vec3 mn, mx;
                        c.have_box = ComputeBodyBox(lb, mn, mx);
                        if (c.have_box) { c.box_min = mn; c.box_max = mx; }
                        c.box_stamp = now_ms();
                    }
                }
            }
            if (c.humanoidAddr)
                c.health = memory->read<float>(c.humanoidAddr + Offsets::Humanoid::Health);
            if (variables::ESP::deadCheck && c.health <= 0.0f) {
                c.isValid = false;
                continue;
            }
            if (updated) {
                const float dx = c.position.X - localPlayerPos.X;
                const float dy = c.position.Y - localPlayerPos.Y;
                const float dz = c.position.Z - localPlayerPos.Z;
                c.distance = sqrtf(dx * dx + dy * dy + dz * dz);
            }
        }
    }

    // sanidad de punteros globales (cambio de lugar / rejoin)
    static auto lastHeal = Clock::now() - std::chrono::seconds(5);
    const bool services_broken =
        !Globals::players.Addr || !Globals::localPlayer.Addr ||
        !Globals::workspace.Addr || !Globals::camera.Addr;
    if (services_broken && now - lastHeal >= std::chrono::milliseconds(200)) {
        lastHeal = now;
        heal_globals();
    }
    if (!Globals::players.Addr || !Globals::localPlayer.Addr) {
        players.clear();
        localRootPrim = 0;
        return;
    }

    // topologia: solo cada 15ms, mas barato que el indice de instancias
    static auto lastTopo = Clock::now() - std::chrono::seconds(10);
    if (now - lastTopo < std::chrono::milliseconds(15))
        return;
    lastTopo = now;

    auto localChar = Globals::localPlayer.GetModelRef();
    auto localRoot = localChar.Addr ? localChar.FindChild("HumanoidRootPart") : RBX::RbxInstance{};
    static auto lastLocalOk = Clock::now();
    if (localRoot.Addr) {
        lastLocalOk = now;
        localRootPrim = localRoot.GetPrimitivePtr();
        localPlayerPos = localRoot.GetPos();
    } else {
        // muerte/respawn del local: no vaciamos todo al instante, dejamos unos
        // segundos de gracia (espectadores siguen dibujandose).
        if (now - lastLocalOk > std::chrono::milliseconds(2500)) {
            players.clear();
            localRootPrim = 0;
        }
        return;
    }
    localPlayerTeam = memory->read<std::uintptr_t>(Globals::localPlayer.Addr + Offsets::Player::Team);

    auto list = Globals::players.GetChildList();
    std::unordered_set<std::uintptr_t> alive;
    alive.reserve(list.size() + 1);
    if (localChar.Addr)
        alive.insert(localChar.Addr);

    for (auto& plr : players)
        plr.isValid = false;

    for (auto& plr : list) {
        if (plr.Addr == Globals::localPlayer.Addr && !variables::ESP::localPlayer)
            continue;
        const auto character = plr.GetModelRef();
        if (!character.Addr)
            continue;
        alive.insert(character.Addr);
        CachedPlayer* slot = nullptr;
        for (auto& c : players) {
            if (c.playerAddr == plr.Addr) {
                slot = &c;
                break;
            }
        }
        if (slot && slot->characterAddr == character.Addr && slot->rootPartAddr) {
            // keepAlive: ya lo movemos en la pasada rapida; aqui solo re-valida
            if (slot->humanoidAddr) {
                const float hp = memory->read<float>(slot->humanoidAddr + Offsets::Humanoid::Health);
                slot->health = hp;
                if (variables::ESP::deadCheck && hp <= 0.0f) {
                    slot->isValid = false;
                    continue;
                }
            }
            if (slot->rootPartAddr) {
                const RBX::Vec3 pos = RBX::RbxInstance(slot->rootPartAddr).GetPos();
                if (std::isfinite(pos.X) && std::isfinite(pos.Y) && std::isfinite(pos.Z))
                    slot->position = pos;
                const float dx = slot->position.X - localPlayerPos.X;
                const float dy = slot->position.Y - localPlayerPos.Y;
                const float dz = slot->position.Z - localPlayerPos.Z;
                slot->distance = sqrtf(dx * dx + dy * dy + dz * dz);
            }
            slot->isValid = true;
            continue;
        }
        const auto& limbs = GetLimbs(character.Addr);
        if (!limbs.hrp || !limbs.humanoid)
            continue;
        const float hp = memory->read<float>(limbs.humanoid + Offsets::Humanoid::Health);
        if (variables::ESP::deadCheck && hp <= 0)
            continue;
        const auto team = memory->read<std::uintptr_t>(plr.Addr + Offsets::Player::Team);
        if (variables::teamCheck && team && team == localPlayerTeam)
            continue;
        CachedPlayer c{};
        c.playerAddr = plr.Addr;
        c.characterAddr = character.Addr;
        c.humanoidAddr = limbs.humanoid;
        c.rootPartAddr = limbs.hrp;
        c.headAddr = limbs.head;
        c.teamAddr = team;
        c.name = plr.GetName();
        c.health = hp;
        c.maxHealth = memory->read<float>(limbs.humanoid + Offsets::Humanoid::MaxHealth);
        c.isR6 = limbs.r6;
        c.isValid = true;
        c.position = RBX::RbxInstance(limbs.hrp).GetPos();
        c.distance = sqrtf((c.position.X - localPlayerPos.X) * (c.position.X - localPlayerPos.X) +
                           (c.position.Y - localPlayerPos.Y) * (c.position.Y - localPlayerPos.Y) +
                           (c.position.Z - localPlayerPos.Z) * (c.position.Z - localPlayerPos.Z));
        RBX::Vec3 mn, mx;
        c.have_box = ComputeBodyBox(limbs, mn, mx);
        if (c.have_box) { c.box_min = mn; c.box_max = mx; }
        c.box_stamp = now_ms();
        for (auto& child : RBX::RbxInstance(character.Addr).GetChildList()) {
            if (child.GetClass() == "Tool") {
                c.tool = child.GetName();
                break;
            }
        }
        if (slot)
            *slot = std::move(c);
        else
            players.push_back(std::move(c));
    }

    players.erase(std::remove_if(players.begin(), players.end(), [](const CachedPlayer& c) { return !c.isValid; }), players.end());

    // si tras una pasada valida no quedo ningun jugador y hay servicios vivos,
    // forzamos re-resolucion (algunos juegos reciclan el arbol de instancias)
    static auto lastEmptyHeal = Clock::now();
    if (players.empty() && !services_broken &&
        now - lastEmptyHeal >= std::chrono::milliseconds(800)) {
        lastEmptyHeal = now;
        heal_globals();
    }

    PruneLimbs(alive);
}
}
